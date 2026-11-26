#include "pkg.h"
#include "orion_tls.h"

#define HTTP_HEADER_MAX 2048
#define HTTP_IO_SIZE 512
#define HTTP_TIMEOUT 3000

struct parsed_url {
    char host[128];
    char path[512];
    uint16_t port;
    in_addr_t address;
    int secure;
};

struct chunk_state {
    char size_text[24];
    size_t size_length;
    uint64_t remaining;
    int state;
};

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

static int parse_port(const char *text, uint16_t *port) {
    if (!*text) return -1;
    unsigned value = 0;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        value = value * 10 + (unsigned)(*p - '0');
        if (value > 65535) return -1;
    }
    if (!value) return -1;
    *port = (uint16_t)value;
    return 0;
}

static int parse_url(const char *url, struct parsed_url *parsed) {
    const char *authority;
    if (strncmp(url, "https://", 8) == 0) {
        parsed->secure = 1;
        parsed->port = 443;
        authority = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        parsed->secure = 0;
        parsed->port = 80;
        authority = url + 7;
    } else {
        return -1;
    }
    const char *cursor = authority;
    while (*cursor && *cursor != '/' && *cursor != '?' && *cursor != '#') ++cursor;
    size_t authority_length = (size_t)(cursor - authority);
    if (!authority_length || authority_length >= sizeof(parsed->host)) return -1;
    const char *colon = NULL;
    for (size_t i = 0; i < authority_length; ++i) {
        if (authority[i] == ':') {
            if (colon) return -1;
            colon = authority + i;
        }
    }
    size_t host_length = colon ? (size_t)(colon - authority) : authority_length;
    if (!host_length || host_length >= sizeof(parsed->host)) return -1;
    memcpy(parsed->host, authority, host_length);
    parsed->host[host_length] = 0;
    if (colon && parse_port(colon + 1, &parsed->port) != 0) return -1;
    size_t used = 0;
    parsed->path[0] = 0;
    if (*cursor != '/') parsed->path[used++] = '/';
    while (*cursor && *cursor != '#') {
        if (used + 1 >= sizeof(parsed->path)) return -1;
        parsed->path[used++] = *cursor++;
    }
    if (!used) parsed->path[used++] = '/';
    parsed->path[used] = 0;
    parsed->address = inet_addr(parsed->host);
    if (parsed->address == INADDR_NONE || parsed->address == INADDR_ANY) {
        uint8_t resolved[4];
        if (orion_dns_lookup_ipv4(parsed->host, NULL, resolved) != 0) return -1;
        memcpy(&parsed->address, resolved, sizeof(parsed->address));
    }
    return 0;
}

static int append_text(char *output, size_t capacity, size_t *used, const char *text) {
    size_t length = strlen(text);
    if (*used + length >= capacity) return -1;
    memcpy(output + *used, text, length);
    *used += length;
    output[*used] = 0;
    return 0;
}

static int append_port(char *output, size_t capacity, size_t *used, uint16_t port) {
    char digits[6];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + port % 10);
        port = (uint16_t)(port / 10);
    } while (port);
    if (*used + count >= capacity) return -1;
    while (count) output[(*used)++] = digits[--count];
    output[*used] = 0;
    return 0;
}

static int build_request(const struct parsed_url *url, char *request, size_t capacity) {
    size_t used = 0;
    request[0] = 0;
    if (append_text(request, capacity, &used, "GET ") != 0 ||
        append_text(request, capacity, &used, url->path) != 0 ||
        append_text(request, capacity, &used, " HTTP/1.1\r\nHost: ") != 0 ||
        append_text(request, capacity, &used, url->host) != 0) return -1;
    uint16_t default_port = url->secure ? 443 : 80;
    if (url->port != default_port && (append_text(request, capacity, &used, ":") != 0 ||
                            append_port(request, capacity, &used, url->port) != 0)) return -1;
    if (append_text(request, capacity, &used,
                    "\r\nUser-Agent: OrionOS-pkg/1.0\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n") != 0) return -1;
    return (int)used;
}

static int wait_receive(int descriptor, int secure, uint8_t *buffer, size_t capacity, uint64_t *last_activity) {
    long amount = secure ? orion_tls_recv(descriptor, buffer, capacity) : recv(descriptor, buffer, capacity, 0);
    if (amount > 0) {
        *last_activity = getticks();
        return (int)amount;
    }
    if (amount == 0) return 0;
    if (secure) return -2;
    if (getticks() - *last_activity >= HTTP_TIMEOUT) return -2;
    sched_yield();
    return -1;
}

static int read_headers(int descriptor, int secure, char *headers, size_t capacity, uint8_t *body, size_t *body_length, size_t *header_length) {
    uint8_t input[HTTP_IO_SIZE];
    size_t used = 0;
    uint64_t last_activity = getticks();
    *body_length = 0;
    for (;;) {
        int amount = wait_receive(descriptor, secure, input, sizeof(input), &last_activity);
        if (amount == -1) continue;
        if (amount <= 0) return -1;
        for (int i = 0; i < amount; ++i) {
            if (used + 1 >= capacity) return -1;
            headers[used++] = (char)input[i];
            if (used >= 4 && headers[used - 4] == '\r' && headers[used - 3] == '\n' &&
                headers[used - 2] == '\r' && headers[used - 1] == '\n') {
                size_t remainder = (size_t)(amount - i - 1);
                memcpy(body, input + i + 1, remainder);
                *body_length = remainder;
                *header_length = used;
                headers[used] = 0;
                return 0;
            }
        }
    }
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

static char *find_crlf(char *text) {
    while (*text) {
        if (text[0] == '\r' && text[1] == '\n') return text;
        ++text;
    }
    return NULL;
}

static int parse_headers(char *headers, int *status, int *has_length, uint64_t *content_length, int *chunked) {
    char *line = headers;
    char *end = find_crlf(line);
    if (!end || strncmp(line, "HTTP/", 5) != 0) return -1;
    *end = 0;
    char *space = strchr(line, ' ');
    if (!space) return -1;
    while (*space == ' ') ++space;
    if (space[0] < '0' || space[0] > '9' || space[1] < '0' || space[1] > '9' || space[2] < '0' || space[2] > '9') return -1;
    *status = (space[0] - '0') * 100 + (space[1] - '0') * 10 + space[2] - '0';
    *has_length = 0;
    *content_length = 0;
    *chunked = 0;
    line = end + 2;
    while (*line && !(line[0] == '\r' && line[1] == '\n')) {
        end = find_crlf(line);
        if (!end) return -1;
        *end = 0;
        char *colon = strchr(line, ':');
        if (colon) {
            size_t name_length = (size_t)(colon - line);
            char *value = colon + 1;
            while (*value == ' ' || *value == '\t') ++value;
            if (name_length == 14 && equal_case(line, "Content-Length", name_length)) {
                if (parse_decimal(value, content_length) != 0) return -1;
                *has_length = 1;
            } else if (name_length == 17 && equal_case(line, "Transfer-Encoding", name_length)) {
                size_t length = strlen(value);
                while (length && (value[length - 1] == ' ' || value[length - 1] == '\t')) value[--length] = 0;
                if (length == 7 && equal_case(value, "chunked", length)) *chunked = 1;
            }
        }
        line = end + 2;
    }
    return 0;
}

static int parse_chunk_size(const char *text, uint64_t *size) {
    uint64_t value = 0;
    size_t digits = 0;
    for (size_t i = 0; text[i] && text[i] != ';'; ++i) {
        unsigned digit;
        if (text[i] >= '0' && text[i] <= '9') digit = (unsigned)(text[i] - '0');
        else if (text[i] >= 'a' && text[i] <= 'f') digit = (unsigned)(text[i] - 'a' + 10);
        else if (text[i] >= 'A' && text[i] <= 'F') digit = (unsigned)(text[i] - 'A' + 10);
        else return -1;
        if (value > (UINT64_MAX - digit) / 16) return -1;
        value = value * 16 + digit;
        ++digits;
    }
    if (!digits) return -1;
    *size = value;
    return 0;
}

static int consume_chunked(int output, const uint8_t *buffer, size_t length, struct chunk_state *state) {
    size_t index = 0;
    while (index < length && state->state != 4) {
        if (state->state == 0) {
            uint8_t value = buffer[index++];
            if (value == '\r') state->state = 1;
            else {
                if (state->size_length + 1 >= sizeof(state->size_text)) return -1;
                state->size_text[state->size_length++] = (char)value;
            }
        } else if (state->state == 1) {
            if (buffer[index++] != '\n') return -1;
            state->size_text[state->size_length] = 0;
            if (parse_chunk_size(state->size_text, &state->remaining) != 0) return -1;
            state->size_length = 0;
            state->state = state->remaining ? 2 : 4;
        } else if (state->state == 2) {
            size_t part = length - index;
            if ((uint64_t)part > state->remaining) part = (size_t)state->remaining;
            if (pkg_write_all(output, buffer + index, part) != 0) return -1;
            index += part;
            state->remaining -= part;
            if (!state->remaining) state->state = 3;
        } else if (state->state == 3) {
            if (buffer[index++] != '\r') return -1;
            state->state = 5;
        } else if (state->state == 5) {
            if (buffer[index++] != '\n') return -1;
            state->state = 0;
        }
    }
    return state->state == 4 ? 1 : 0;
}

static void close_http_network(int descriptor, int secure) {
    if (secure) orion_tls_close(descriptor);
    close(descriptor);
}

int pkg_http_get(const char *url, const char *output_path) {
    struct parsed_url parsed;
    if (parse_url(url, &parsed) != 0) {
        printf("pkg: invalid or unsupported URL: %s\n", url);
        return -1;
    }
    char request[1024];
    int request_length = build_request(&parsed, request, sizeof(request));
    if (request_length < 0) return -1;
    int network = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
    if (network < 0) return -1;
    if (parsed.secure) printf("pkg: WARNING: TLS certificate verification is disabled; connection may be intercepted.\n");
    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(parsed.port);
    destination.sin_addr.s_addr = parsed.address;
    if (connect(network, (const struct sockaddr *)&destination, sizeof(destination)) != 0) {
        close_http_network(network, parsed.secure);
        return -1;
    }
    if (parsed.secure && orion_tls_connect(network, parsed.host) != 0) {
        close_http_network(network, parsed.secure);
        return -1;
    }
    size_t sent = 0;
    uint64_t last_activity = getticks();
    while (sent < (size_t)request_length) {
        long amount = parsed.secure ? orion_tls_send(network, request + sent, (size_t)request_length - sent) :
                                      send(network, request + sent, (size_t)request_length - sent, 0);
        if (amount > 0) {
            sent += (size_t)amount;
            last_activity = getticks();
        } else if (getticks() - last_activity >= HTTP_TIMEOUT) {
            close_http_network(network, parsed.secure);
            return -1;
        } else {
            sched_yield();
        }
    }
    char headers[HTTP_HEADER_MAX + 1];
    uint8_t initial[HTTP_IO_SIZE];
    size_t initial_length = 0;
    size_t header_length = 0;
    if (read_headers(network, parsed.secure, headers, sizeof(headers), initial, &initial_length, &header_length) != 0) {
        close_http_network(network, parsed.secure);
        return -1;
    }
    int status = 0;
    int has_length = 0;
    uint64_t expected = 0;
    int chunked = 0;
    if (parse_headers(headers, &status, &has_length, &expected, &chunked) != 0 || status < 200 || status >= 300) {
        printf("pkg: HTTP request failed with status %u\n", (unsigned)status);
        close_http_network(network, parsed.secure);
        return -1;
    }
    int output = open(output_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (output < 0) {
        close_http_network(network, parsed.secure);
        return -1;
    }
    int result = 0;
    last_activity = getticks();
    if (chunked) {
        struct chunk_state state;
        memset(&state, 0, sizeof(state));
        int complete = consume_chunked(output, initial, initial_length, &state);
        uint8_t buffer[HTTP_IO_SIZE];
        while (complete == 0) {
            int amount = wait_receive(network, parsed.secure, buffer, sizeof(buffer), &last_activity);
            if (amount == -1) continue;
            if (amount <= 0) {
                result = -1;
                break;
            }
            complete = consume_chunked(output, buffer, (size_t)amount, &state);
            if (complete < 0) result = -1;
        }
        if (complete < 0) result = -1;
    } else if (has_length) {
        uint64_t remaining = expected;
        size_t first = initial_length;
        if ((uint64_t)first > remaining) first = (size_t)remaining;
        if (first && pkg_write_all(output, initial, first) != 0) result = -1;
        remaining -= first;
        uint8_t buffer[HTTP_IO_SIZE];
        while (remaining && !result) {
            int amount = wait_receive(network, parsed.secure, buffer, sizeof(buffer), &last_activity);
            if (amount == -1) continue;
            if (amount <= 0) {
                result = -1;
                break;
            }
            size_t part = (uint64_t)amount > remaining ? (size_t)remaining : (size_t)amount;
            if (pkg_write_all(output, buffer, part) != 0) {
                result = -1;
                break;
            }
            remaining -= part;
        }
    } else {
        if (initial_length && pkg_write_all(output, initial, initial_length) != 0) result = -1;
        uint8_t buffer[HTTP_IO_SIZE];
        while (!result) {
            int amount = wait_receive(network, parsed.secure, buffer, sizeof(buffer), &last_activity);
            if (amount == -1) continue;
            if (amount == 0) break;
            if (amount < 0 || pkg_write_all(output, buffer, (size_t)amount) != 0) {
                result = -1;
                break;
            }
        }
    }
    close(output);
    close_http_network(network, parsed.secure);
    if (result != 0) {
        unlink(output_path);
        return -1;
    }
    (void)header_length;
    return 0;
}
