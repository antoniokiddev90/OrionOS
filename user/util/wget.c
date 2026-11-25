#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dns.h>
#include <sys/socket.h>
#include <unistd.h>

#define HEADER_CAPACITY 2048
#define NETWORK_BUFFER_SIZE 512
#define RECEIVE_TIMEOUT_TICKS 3000

enum {
    CHUNK_SIZE_LINE,
    CHUNK_SIZE_LF,
    CHUNK_DATA,
    CHUNK_DATA_CR,
    CHUNK_DATA_LF,
    CHUNK_DONE
};

struct chunk_decoder {
    char size_text[32];
    size_t size_length;
    uint64_t remaining;
    uint64_t written;
    int state;
    int extension;
};

static int append_text(char *destination, size_t capacity, size_t *used, const char *text) {
    size_t length = strlen(text);
    if (*used + length >= capacity) return -1;
    memcpy(destination + *used, text, length);
    *used += length;
    destination[*used] = 0;
    return 0;
}

static int append_number(char *destination, size_t capacity, size_t *used, unsigned value) {
    char digits[10];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(digits));
    if (*used + count >= capacity) return -1;
    while (count) destination[(*used)++] = digits[--count];
    destination[*used] = 0;
    return 0;
}

static int parse_port(const char *text, uint16_t *port) {
    if (!*text) return -1;
    unsigned value = 0;
    for (const char *cursor = text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return -1;
        value = value * 10 + (unsigned)(*cursor - '0');
        if (value > 65535) return -1;
    }
    if (!value) return -1;
    *port = (uint16_t)value;
    return 0;
}

static int parse_url(const char *url, char *host, size_t host_capacity, char *path, size_t path_capacity,
                     uint16_t *port) {
    if (strncmp(url, "http://", 7) != 0) return -1;
    const char *authority = url + 7;
    const char *cursor = authority;
    while (*cursor && *cursor != '/' && *cursor != '?' && *cursor != '#') ++cursor;
    size_t authority_length = (size_t)(cursor - authority);
    if (!authority_length || authority_length >= host_capacity) return -1;

    const char *colon = NULL;
    for (size_t i = 0; i < authority_length; ++i) {
        if (authority[i] == ':') {
            if (colon) return -1;
            colon = authority + i;
        }
    }
    size_t host_length = colon ? (size_t)(colon - authority) : authority_length;
    if (!host_length || host_length >= host_capacity) return -1;
    memcpy(host, authority, host_length);
    host[host_length] = 0;
    *port = 80;
    if (colon && parse_port(colon + 1, port) != 0) return -1;

    size_t path_used = 0;
    path[0] = 0;
    if (*cursor != '/') {
        if (append_text(path, path_capacity, &path_used, "/") != 0) return -1;
        if (*cursor == '?') {
            while (*cursor && *cursor != '#') {
                char single[2] = {*cursor++, 0};
                if (append_text(path, path_capacity, &path_used, single) != 0) return -1;
            }
        }
    } else {
        while (*cursor && *cursor != '#') {
            char single[2] = {*cursor++, 0};
            if (append_text(path, path_capacity, &path_used, single) != 0) return -1;
        }
    }
    return 0;
}

static int choose_filename(const char *path, char *filename, size_t capacity) {
    const char *segment = path;
    for (const char *cursor = path; *cursor; ++cursor) {
        if (*cursor == '/') segment = cursor + 1;
    }
    size_t length = 0;
    while (segment[length] && segment[length] != '?') ++length;
    if (!length) {
        if (capacity < sizeof("index.html")) return -1;
        strcpy(filename, "index.html");
        return 0;
    }
    if (length >= capacity) return -1;
    memcpy(filename, segment, length);
    filename[length] = 0;
    return 0;
}

static int append_header(char *request, size_t capacity, size_t *used, const char *text) {
    return append_text(request, capacity, used, text);
}

static int build_request(char *request, size_t capacity, const char *path, const char *host, uint16_t port) {
    size_t used = 0;
    request[0] = 0;
    if (append_header(request, capacity, &used, "GET ") != 0 ||
        append_header(request, capacity, &used, path) != 0 ||
        append_header(request, capacity, &used, " HTTP/1.0\r\nHost: ") != 0 ||
        append_header(request, capacity, &used, host) != 0) return -1;
    if (port != 80 && (append_header(request, capacity, &used, ":") != 0 ||
                       append_number(request, capacity, &used, port) != 0)) return -1;
    if (append_header(request, capacity, &used, "\r\nUser-Agent: OrionOS wget/1.0\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n") != 0) return -1;
    return (int)used;
}

static char *find_crlf(char *text) {
    while (*text) {
        if (text[0] == '\r' && text[1] == '\n') return text;
        ++text;
    }
    return NULL;
}

static int read_response_headers(int descriptor, char *headers, size_t capacity,
                                 uint8_t *initial_body, size_t *initial_length, size_t *header_length) {
    uint8_t buffer[NETWORK_BUFFER_SIZE];
    size_t used = 0;
    uint64_t last_activity = getticks();
    *initial_length = 0;
    for (;;) {
        ssize_t length = recv(descriptor, buffer, sizeof(buffer), 0);
        if (length == 0) return -1;
        if (length < 0) {
            if (getticks() - last_activity >= RECEIVE_TIMEOUT_TICKS) return -1;
            sched_yield();
            continue;
        }
        last_activity = getticks();
        for (ssize_t i = 0; i < length; ++i) {
            if (used >= capacity - 1) return -1;
            headers[used++] = (char)buffer[i];
            if (used >= 4 && headers[used - 4] == '\r' && headers[used - 3] == '\n' &&
                headers[used - 2] == '\r' && headers[used - 1] == '\n') {
                size_t remainder = (size_t)(length - i - 1);
                if (remainder > NETWORK_BUFFER_SIZE) return -1;
                memcpy(initial_body, buffer + i + 1, remainder);
                *initial_length = remainder;
                *header_length = used;
                headers[used - 2] = 0;
                return 0;
            }
        }
    }
}

static int equal_case(const char *left, const char *right, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static int parse_decimal(const char *text, uint64_t *value) {
    while (*text == ' ' || *text == '\t') ++text;
    if (*text < '0' || *text > '9') return -1;
    uint64_t result = 0;
    while (*text >= '0' && *text <= '9') {
        unsigned digit = (unsigned)(*text++ - '0');
        if (result > (UINT64_MAX - digit) / 10) return -1;
        result = result * 10 + digit;
    }
    while (*text == ' ' || *text == '\t') ++text;
    if (*text) return -1;
    *value = result;
    return 0;
}

static int parse_response_headers(char *headers, size_t header_length, int *status,
                                  int *has_content_length, uint64_t *content_length, int *chunked) {
    char *line_end = find_crlf(headers);
    if (!line_end) return -1;
    *line_end = 0;
    if (strncmp(headers, "HTTP/", 5) != 0) return -1;
    char *code = strchr(headers, ' ');
    if (!code) return -1;
    while (*code == ' ') ++code;
    if (code[0] < '0' || code[0] > '9' || code[1] < '0' || code[1] > '9' || code[2] < '0' || code[2] > '9') return -1;
    *status = (code[0] - '0') * 100 + (code[1] - '0') * 10 + code[2] - '0';
    *has_content_length = 0;
    *content_length = 0;
    *chunked = 0;
    char *cursor = line_end + 2;
    char *limit = headers + header_length - 2;
    while (cursor < limit && *cursor) {
        line_end = find_crlf(cursor);
        if (!line_end || line_end == cursor) break;
        *line_end = 0;
        char *colon = strchr(cursor, ':');
        if (colon) {
            size_t name_length = (size_t)(colon - cursor);
            const char *value = colon + 1;
            while (*value == ' ' || *value == '\t') ++value;
            if (name_length == 14 && equal_case(cursor, "Content-Length", name_length)) {
                if (parse_decimal(value, content_length) != 0) return -1;
                *has_content_length = 1;
            } else if (name_length == 17 && equal_case(cursor, "Transfer-Encoding", name_length)) {
                size_t value_length = strlen(value);
                while (value_length && (value[value_length - 1] == ' ' || value[value_length - 1] == '\t')) --value_length;
                if (value_length == 7 && equal_case(value, "chunked", value_length)) *chunked = 1;
            }
        }
        cursor = line_end + 2;
    }
    return 0;
}

static int write_all(int descriptor, const uint8_t *buffer, size_t length) {
    size_t written = 0;
    while (written < length) {
        ssize_t result = write(descriptor, buffer + written, length - written);
        if (result <= 0) return -1;
        written += (size_t)result;
    }
    return 0;
}

static int parse_chunk_size(const char *text, uint64_t *size) {
    uint64_t result = 0;
    size_t digits = 0;
    for (size_t i = 0; text[i] && text[i] != ';'; ++i) {
        unsigned value;
        if (text[i] >= '0' && text[i] <= '9') value = (unsigned)(text[i] - '0');
        else if (text[i] >= 'a' && text[i] <= 'f') value = (unsigned)(text[i] - 'a' + 10);
        else if (text[i] >= 'A' && text[i] <= 'F') value = (unsigned)(text[i] - 'A' + 10);
        else return -1;
        if (result > (UINT64_MAX - value) / 16) return -1;
        result = result * 16 + value;
        ++digits;
    }
    if (!digits) return -1;
    *size = result;
    return 0;
}

static int consume_chunked(int output, const uint8_t *buffer, size_t length, struct chunk_decoder *decoder) {
    size_t index = 0;
    while (index < length && decoder->state != CHUNK_DONE) {
        if (decoder->state == CHUNK_SIZE_LINE) {
            uint8_t value = buffer[index++];
            if (value == '\r') {
                decoder->state = CHUNK_SIZE_LF;
            } else if (value == ';') {
                decoder->extension = 1;
            } else if (!decoder->extension) {
                if (decoder->size_length + 1 >= sizeof(decoder->size_text)) return -1;
                decoder->size_text[decoder->size_length++] = (char)value;
            }
        } else if (decoder->state == CHUNK_SIZE_LF) {
            if (buffer[index++] != '\n') return -1;
            decoder->size_text[decoder->size_length] = 0;
            if (parse_chunk_size(decoder->size_text, &decoder->remaining) != 0) return -1;
            decoder->size_length = 0;
            decoder->extension = 0;
            decoder->state = decoder->remaining ? CHUNK_DATA : CHUNK_DONE;
        } else if (decoder->state == CHUNK_DATA) {
            size_t amount = length - index;
            if ((uint64_t)amount > decoder->remaining) amount = (size_t)decoder->remaining;
            if (write_all(output, buffer + index, amount) != 0) return -1;
            decoder->remaining -= amount;
            decoder->written += amount;
            index += amount;
            if (!decoder->remaining) decoder->state = CHUNK_DATA_CR;
        } else if (decoder->state == CHUNK_DATA_CR) {
            if (buffer[index++] != '\r') return -1;
            decoder->state = CHUNK_DATA_LF;
        } else if (decoder->state == CHUNK_DATA_LF) {
            if (buffer[index++] != '\n') return -1;
            decoder->state = CHUNK_SIZE_LINE;
        }
    }
    return decoder->state == CHUNK_DONE ? 1 : 0;
}

static int receive_data(int descriptor, uint8_t *buffer, size_t capacity, uint64_t *last_activity) {
    ssize_t result = recv(descriptor, buffer, capacity, 0);
    if (result > 0) {
        *last_activity = getticks();
        return (int)result;
    }
    if (result == 0) return 0;
    if (getticks() - *last_activity >= RECEIVE_TIMEOUT_TICKS) return -2;
    sched_yield();
    return -1;
}

int main(int argc, char **argv) {
    const char *url = NULL;
    const char *requested_output = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-O") == 0) {
            if (++i >= argc || requested_output) {
                puts("Usage: wget [-O file] http://host[:port]/path");
                return 2;
            }
            requested_output = argv[i];
        } else if (argv[i][0] == '-' || url) {
            puts("Usage: wget [-O file] http://host[:port]/path");
            return 2;
        } else {
            url = argv[i];
        }
    }
    if (!url) {
        puts("Usage: wget [-O file] http://host[:port]/path");
        return 2;
    }

    char host[254];
    char path[512];
    uint16_t port;
    in_addr_t address;
    if (parse_url(url, host, sizeof(host), path, sizeof(path), &port) != 0) {
        puts("wget: only HTTP URLs with a valid hostname or IPv4 address are supported");
        return 2;
    }
    address = inet_addr(host);
    if (address == INADDR_NONE || address == INADDR_ANY) {
        uint8_t resolved[4];
        if (orion_dns_lookup_ipv4(host, NULL, resolved) != 0) {
            puts("wget: DNS lookup failed");
            return 1;
        }
        memcpy(&address, resolved, sizeof(address));
    }

    char output_path[256];
    if (requested_output) {
        size_t length = strlen(requested_output);
        if (!length || length >= sizeof(output_path)) {
            puts("wget: invalid output path");
            return 2;
        }
        memcpy(output_path, requested_output, length + 1);
    } else if (choose_filename(path, output_path, sizeof(output_path)) != 0) {
        puts("wget: URL path is too long");
        return 2;
    }

    char request[1024];
    int request_length = build_request(request, sizeof(request), path, host, port);
    if (request_length < 0) {
        puts("wget: request is too large");
        return 2;
    }

    int network = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    if (network < 0) {
        puts("wget: cannot create TCP socket");
        return 1;
    }
    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    destination.sin_addr.s_addr = address;
    printf("Connecting to %s:%u\n", host, (unsigned)port);
    if (connect(network, (const struct sockaddr *)&destination, sizeof(destination)) != 0) {
        puts("wget: connection failed");
        close(network);
        return 1;
    }

    size_t sent = 0;
    while (sent < (size_t)request_length) {
        ssize_t amount = send(network, request + sent, (size_t)request_length - sent, 0);
        if (amount <= 0) {
            puts("wget: request transmission failed");
            close(network);
            return 1;
        }
        sent += (size_t)amount;
    }

    char headers[HEADER_CAPACITY + 1];
    uint8_t initial_body[NETWORK_BUFFER_SIZE];
    size_t initial_length = 0;
    size_t header_length = 0;
    if (read_response_headers(network, headers, sizeof(headers), initial_body,
                              &initial_length, &header_length) != 0) {
        puts("wget: failed to read HTTP response headers");
        close(network);
        return 1;
    }

    int status = 0;
    int has_content_length = 0;
    uint64_t content_length = 0;
    int chunked = 0;
    if (parse_response_headers(headers, header_length, &status, &has_content_length,
                               &content_length, &chunked) != 0 || status < 200 || status >= 300) {
        printf("wget: HTTP request failed with status %u\n", (unsigned)status);
        close(network);
        return 1;
    }

    int output = open(output_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (output < 0) {
        puts("wget: cannot open output file");
        close(network);
        return 1;
    }

    uint64_t downloaded = 0;
    int result = 0;
    uint64_t last_activity = getticks();
    if (chunked) {
        struct chunk_decoder decoder;
        memset(&decoder, 0, sizeof(decoder));
        decoder.state = CHUNK_SIZE_LINE;
        int consumed = consume_chunked(output, initial_body, initial_length, &decoder);
        uint8_t buffer[NETWORK_BUFFER_SIZE];
        while (consumed == 0) {
            int amount = receive_data(network, buffer, sizeof(buffer), &last_activity);
            if (amount == -1) continue;
            if (amount <= 0) {
                result = -1;
                break;
            }
            consumed = consume_chunked(output, buffer, (size_t)amount, &decoder);
            if (consumed < 0) result = -1;
        }
        if (consumed < 0) result = -1;
        downloaded = decoder.written;
    } else if (has_content_length) {
        uint64_t remaining = content_length;
        size_t first = initial_length;
        if ((uint64_t)first > remaining) first = (size_t)remaining;
        if (first && write_all(output, initial_body, first) != 0) result = -1;
        downloaded += first;
        remaining -= first;
        uint8_t buffer[NETWORK_BUFFER_SIZE];
        while (remaining && !result) {
            int amount = receive_data(network, buffer, sizeof(buffer), &last_activity);
            if (amount == -1) continue;
            if (amount <= 0) {
                result = -1;
                break;
            }
            size_t copied = (uint64_t)amount > remaining ? (size_t)remaining : (size_t)amount;
            if (write_all(output, buffer, copied) != 0) {
                result = -1;
                break;
            }
            downloaded += copied;
            remaining -= copied;
        }
    } else {
        if (initial_length && write_all(output, initial_body, initial_length) != 0) result = -1;
        downloaded += initial_length;
        uint8_t buffer[NETWORK_BUFFER_SIZE];
        while (!result) {
            int amount = receive_data(network, buffer, sizeof(buffer), &last_activity);
            if (amount == -1) continue;
            if (amount == 0) break;
            if (amount < 0 || write_all(output, buffer, (size_t)amount) != 0) {
                result = -1;
                break;
            }
            downloaded += (unsigned)amount;
        }
    }

    close(output);
    close(network);
    if (result != 0) {
        puts("wget: download was incomplete or the response was malformed");
        return 1;
    }
    printf("Saved %lu bytes to %s\n", (unsigned long)downloaded, output_path);
    return 0;
}
