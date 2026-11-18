#ifndef ORION_UTIL_COMMON_H
#define ORION_UTIL_COMMON_H
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
static int __attribute__((unused)) util_copy_file(const char *source, const char *destination) {
    int input = open(source, O_RDONLY);
    if (input < 0) return -1;
    int output = open(destination, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (output < 0) { close(input); return -1; }
    char buffer[512];
    int result = 0;
    ssize_t count;
    while ((count = read(input, buffer, sizeof(buffer))) > 0) {
        size_t sent = 0;
        while (sent < (size_t)count) {
            ssize_t written = write(output, buffer + sent, (size_t)count - sent);
            if (written <= 0) { result = -1; break; }
            sent += (size_t)written;
        }
        if (result) break;
    }
    if (count < 0) result = -1;
    close(input);
    close(output);
    return result;
}
static int __attribute__((unused)) util_print_file(const char *path) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    char buffer[256];
    ssize_t count;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) write(STDOUT_FILENO, buffer, (size_t)count);
    close(descriptor);
    return count < 0 ? -1 : 0;
}
#define UTIL_ECHO_OFF 0
#define UTIL_ECHO_ON 1

struct util_account {
    char name[32];
    uint32_t uid;
    uint32_t gid;
    char home[128];
    char shell[64];
};

static int __attribute__((unused)) util_read_line(int descriptor, char *buffer, size_t capacity, int echo) {
    size_t length = 0;
    char byte;
    while (length + 1 < capacity) {
        if (read(descriptor, &byte, 1) <= 0) break;
        if (byte == '\n') {
            if (echo) putchar('\n');
            break;
        }
        if (byte == '\b') {
            if (length) {
                --length;
                if (echo) write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }
        buffer[length++] = byte;
        if (echo) putchar(byte);
    }
    buffer[length] = 0;
    return (int)length;
}

static int __attribute__((unused)) util_split(char *line, char separator, char **fields, int capacity) {
    int count = 0;
    fields[count++] = line;
    for (char *position = line; *position && count < capacity; ++position) {
        if (*position == separator) {
            *position = 0;
            fields[count++] = position + 1;
        }
    }
    return count;
}

static int __attribute__((unused)) util_find_account(const char *name, uint32_t uid, int by_name, struct util_account *account) {
    int descriptor = open("/etc/passwd", O_RDONLY, 0);
    if (descriptor < 0) return -1;
    char line[256];
    int found = -1;
    while (found != 0 && util_read_line(descriptor, line, sizeof(line), UTIL_ECHO_OFF) > 0) {
        char *fields[8];
        if (util_split(line, ':', fields, 8) < 7) continue;
        if (by_name ? strcmp(fields[0], name) != 0 : (uint32_t)atoi(fields[2]) != uid) continue;
        strncpy(account->name, fields[0], sizeof(account->name) - 1);
        account->name[sizeof(account->name) - 1] = 0;
        account->uid = (uint32_t)atoi(fields[2]);
        account->gid = (uint32_t)atoi(fields[3]);
        strncpy(account->home, fields[5], sizeof(account->home) - 1);
        account->home[sizeof(account->home) - 1] = 0;
        strncpy(account->shell, fields[6], sizeof(account->shell) - 1);
        account->shell[sizeof(account->shell) - 1] = 0;
        found = 0;
    }
    close(descriptor);
    return found;
}

static const uint32_t util_sha256_k[64] __attribute__((unused)) = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t __attribute__((unused)) util_ror(uint32_t value, int count) {
    return (value >> count) | (value << (32 - count));
}

static void __attribute__((unused)) util_sha256_block(uint32_t *state, const uint8_t *block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
               ((uint32_t)block[4 * i + 2] << 8) | (uint32_t)block[4 * i + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = util_ror(w[i - 15], 7) ^ util_ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = util_ror(w[i - 2], 17) ^ util_ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = util_ror(e, 6) ^ util_ror(e, 11) ^ util_ror(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + util_sha256_k[i] + w[i];
        uint32_t s0 = util_ror(a, 2) ^ util_ror(a, 13) ^ util_ror(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static int __attribute__((unused)) util_sha256(const uint8_t *data, size_t length, uint8_t *digest) {
    if (length > 200) return -1;
    uint8_t buffer[256];
    for (size_t i = 0; i < length; ++i) buffer[i] = data[i];
    buffer[length] = 0x80;
    size_t padded = ((length + 9 + 63) / 64) * 64;
    for (size_t i = length + 1; i < padded; ++i) buffer[i] = 0;
    uint64_t bits = (uint64_t)length * 8;
    for (int i = 0; i < 8; ++i) buffer[padded - 1 - i] = (uint8_t)(bits >> (8 * i));
    uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (size_t offset = 0; offset < padded; offset += 64) util_sha256_block(state, buffer + offset);
    for (int i = 0; i < 8; ++i) {
        digest[4 * i] = (uint8_t)(state[i] >> 24);
        digest[4 * i + 1] = (uint8_t)(state[i] >> 16);
        digest[4 * i + 2] = (uint8_t)(state[i] >> 8);
        digest[4 * i + 3] = (uint8_t)state[i];
    }
    return 0;
}

static void __attribute__((unused)) util_to_hex(const uint8_t *bytes, size_t length, char *out) {
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * length] = 0;
}

static int __attribute__((unused)) util_hash_password(const char *salt, const char *password, char *out) {
    uint8_t message[200];
    size_t length = 0;
    for (const char *p = salt; *p; ++p) {
        if (length >= sizeof(message)) return -1;
        message[length++] = (uint8_t)*p;
    }
    for (const char *p = password; *p; ++p) {
        if (length >= sizeof(message)) return -1;
        message[length++] = (uint8_t)*p;
    }
    uint8_t digest[32];
    if (util_sha256(message, length, digest) != 0) return -1;
    util_to_hex(digest, sizeof(digest), out);
    return 0;
}

static void __attribute__((unused)) util_random_salt(char *out) {
    static const char digits[] = "0123456789abcdef";
    uint64_t state = getticks() ^ ((uint64_t)getpid() << 32) ^ 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 8; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        out[2 * i] = digits[(state >> 4) & 15];
        out[2 * i + 1] = digits[state & 15];
    }
    out[16] = 0;
}

static int __attribute__((unused)) util_make_entry(const char *password, char *out, size_t capacity) {
    char salt[17];
    char hash[65];
    util_random_salt(salt);
    if (util_hash_password(salt, password, hash) != 0) return -1;
    if (strlen(salt) + strlen(hash) + 9 > capacity) return -1;
    strcpy(out, "$sha256$");
    strcat(out, salt);
    strcat(out, "$");
    strcat(out, hash);
    return 0;
}

static int __attribute__((unused)) util_verify_entry(char *stored, const char *password) {
    if (strncmp(stored, "$sha256$", 8) != 0) return -1;
    char *salt = stored + 8;
    char *end = salt;
    while (*end && *end != '$') ++end;
    if (*end != '$') return -1;
    *end = 0;
    char hash[65];
    if (util_hash_password(salt, password, hash) != 0) return -1;
    return strcmp(hash, end + 1) == 0 ? 0 : -1;
}

static int __attribute__((unused)) util_check_password(const char *name, const char *password) {
    int descriptor = open("/etc/shadow", O_RDONLY, 0);
    if (descriptor < 0) return -1;
    char line[256];
    int result = -1;
    while (result != 0 && util_read_line(descriptor, line, sizeof(line), UTIL_ECHO_OFF) > 0) {
        char *fields[2];
        if (util_split(line, ':', fields, 2) < 2) continue;
        if (strcmp(fields[0], name) != 0) continue;
        result = util_verify_entry(fields[1], password);
    }
    close(descriptor);
    return result;
}

static int __attribute__((unused)) util_shadow_store(const char *name, const char *entry) {
    int descriptor = open("/etc/shadow", O_RDONLY, 0);
    if (descriptor < 0) return -1;
    static char content[2048];
    static char output[2048];
    size_t total = 0;
    ssize_t count;
    while (total < sizeof(content) - 1 && (count = read(descriptor, content + total, sizeof(content) - 1 - total)) > 0) {
        total += (size_t)count;
    }
    content[total] = 0;
    close(descriptor);
    size_t used = 0;
    int found = 0;
    size_t name_length = strlen(name);
    char *line = content;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') ++end;
        char saved = *end;
        *end = 0;
        if (strncmp(line, name, name_length) == 0 && line[name_length] == ':') {
            found = 1;
            for (size_t i = 0; name[i] && used < sizeof(output) - 1; ++i) output[used++] = name[i];
            output[used++] = ':';
            for (size_t i = 0; entry[i] && used < sizeof(output) - 1; ++i) output[used++] = entry[i];
        } else {
            for (char *p = line; *p && used < sizeof(output) - 1; ++p) output[used++] = *p;
        }
        output[used++] = '\n';
        *end = saved;
        if (!saved) break;
        line = end + 1;
    }
    if (!found) {
        for (size_t i = 0; name[i] && used < sizeof(output) - 1; ++i) output[used++] = name[i];
        output[used++] = ':';
        for (size_t i = 0; entry[i] && used < sizeof(output) - 1; ++i) output[used++] = entry[i];
        output[used++] = '\n';
    }
    descriptor = open("/etc/shadow", O_WRONLY | O_TRUNC, 0);
    if (descriptor < 0) return -1;
    size_t sent = 0;
    while (sent < used) {
        ssize_t written = write(descriptor, output + sent, used - sent);
        if (written <= 0) break;
        sent += (size_t)written;
    }
    close(descriptor);
    return sent == used ? 0 : -1;
}

static int __attribute__((unused)) util_in_sudoers(const char *name) {
    int descriptor = open("/etc/sudoers", O_RDONLY, 0);
    if (descriptor < 0) return 0;
    char line[64];
    int found = 0;
    while (!found && util_read_line(descriptor, line, sizeof(line), UTIL_ECHO_OFF) > 0) {
        if (strcmp(line, name) == 0) found = 1;
    }
    close(descriptor);
    return found;
}

static uint32_t __attribute__((unused)) util_next_uid(void) {
    uint32_t next = 1000;
    int descriptor = open("/etc/passwd", O_RDONLY, 0);
    if (descriptor < 0) return next;
    char line[256];
    while (util_read_line(descriptor, line, sizeof(line), UTIL_ECHO_OFF) > 0) {
        char *fields[8];
        if (util_split(line, ':', fields, 8) < 7) continue;
        uint32_t uid = (uint32_t)atoi(fields[2]);
        if (uid >= next) next = uid + 1;
    }
    close(descriptor);
    return next;
}

static size_t __attribute__((unused)) util_format_uint(uint32_t value, char *out) {
    char reverse[16];
    size_t count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    size_t length = count;
    for (size_t i = 0; i < length; ++i) out[i] = reverse[length - 1 - i];
    out[length] = 0;
    return length;
}

#endif
