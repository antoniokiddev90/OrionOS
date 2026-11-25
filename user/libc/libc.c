#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syscalls.h>
#include <sys/socket.h>
#include <sys/network.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define LIBC_HEAP_SIZE 65536
static unsigned char libc_heap[LIBC_HEAP_SIZE];
static size_t libc_heap_used;
long orion_syscall0(uint64_t number) {
    uint64_t result;
    __asm__ volatile("int $0x9E" : "=a"(result) : "a"(number) : "rcx", "r11", "memory");
    return (long)result;
}
long orion_syscall1(uint64_t number, uint64_t first) {
    uint64_t result;
    __asm__ volatile("int $0x9E" : "=a"(result) : "a"(number), "D"(first) : "rcx", "r11", "memory");
    return (long)result;
}
long orion_syscall2(uint64_t number, uint64_t first, uint64_t second) {
    uint64_t result;
    __asm__ volatile("int $0x9E" : "=a"(result) : "a"(number), "D"(first), "S"(second) : "rcx", "r11", "memory");
    return (long)result;
}
long orion_syscall3(uint64_t number, uint64_t first, uint64_t second, uint64_t third) {
    uint64_t result;
    __asm__ volatile("int $0x9E" : "=a"(result) : "a"(number), "D"(first), "S"(second), "d"(third) : "rcx", "r11", "memory");
    return (long)result;
}
ssize_t read(int descriptor, void *buffer, size_t size) { return (ssize_t)orion_syscall3(ORION_SYS_READ, (uint64_t)descriptor, (uint64_t)buffer, size); }
ssize_t write(int descriptor, const void *buffer, size_t size) { return (ssize_t)orion_syscall3(ORION_SYS_WRITE, (uint64_t)descriptor, (uint64_t)buffer, size); }
int open(const char *path, int flags, ...) {
    uint32_t mode = 0;
    if (flags & O_CREAT) {
        __builtin_va_list arguments;
        __builtin_va_start(arguments, flags);
        mode = (uint32_t)__builtin_va_arg(arguments, unsigned int);
        __builtin_va_end(arguments);
    }
    return (int)orion_syscall3(ORION_SYS_OPEN, (uint64_t)path, (uint64_t)flags, mode);
}
int close(int descriptor) { return (int)orion_syscall1(ORION_SYS_CLOSE, (uint64_t)descriptor); }
off_t lseek(int descriptor, off_t offset, int whence) { return (off_t)orion_syscall3(ORION_SYS_LSEEK, (uint64_t)descriptor, (uint64_t)offset, (uint64_t)whence); }
pid_t fork(void) { return (pid_t)orion_syscall0(ORION_SYS_FORK); }
int execve(const char *path, char *const arguments[], char *const environment[]) {
    (void)environment;
    return (int)orion_syscall2(ORION_SYS_EXECVE, (uint64_t)path, (uint64_t)arguments);
}
pid_t wait4(pid_t pid, int *status, int options, void *usage) {
    (void)usage;
    for (;;) {
        long result = orion_syscall3(ORION_SYS_WAIT4, (uint64_t)(int64_t)pid, (uint64_t)status, (uint64_t)options);
        if (result != 0 || (options & 1)) return (pid_t)result;
        sched_yield();
    }
}
pid_t waitpid(pid_t pid, int *status, int options) { return wait4(pid, status, options, NULL); }
int getdents64(int descriptor, void *buffer, size_t size) { return (int)orion_syscall3(ORION_SYS_GETDENTS64, (uint64_t)descriptor, (uint64_t)buffer, size); }
int chdir(const char *path) { return (int)orion_syscall1(ORION_SYS_CHDIR, (uint64_t)path); }
char *getcwd(char *buffer, size_t size) { return (char *)orion_syscall2(ORION_SYS_GETCWD, (uint64_t)buffer, size); }
pid_t getpid(void) { return (pid_t)orion_syscall0(ORION_SYS_GETPID); }
void sched_yield(void) { (void)orion_syscall0(ORION_SYS_YIELD); }
int dup(int descriptor) { return (int)orion_syscall1(ORION_SYS_DUP, (uint64_t)descriptor); }
int access(const char *path, int mode) { return (int)orion_syscall2(ORION_SYS_ACCESS, (uint64_t)path, (uint64_t)mode); }
uid_t getuid(void) { return (uid_t)orion_syscall0(ORION_SYS_GETUID); }
gid_t getgid(void) { return (gid_t)orion_syscall0(ORION_SYS_GETGID); }
int setuid(uid_t uid) { return (int)orion_syscall1(ORION_SYS_SETUID, (uint64_t)uid); }
int setgid(gid_t gid) { return (int)orion_syscall1(ORION_SYS_SETGID, (uint64_t)gid); }
uid_t geteuid(void) { return (uid_t)orion_syscall0(ORION_SYS_GETEUID); }
gid_t getegid(void) { return (gid_t)orion_syscall0(ORION_SYS_GETEGID); }
int chown(const char *path, uid_t uid, gid_t gid) { return (int)orion_syscall3(ORION_SYS_CHOWN, (uint64_t)path, (uint64_t)uid, (uint64_t)gid); }
uint64_t getticks(void) { return (uint64_t)orion_syscall0(ORION_SYS_GETTICKS); }
int orion_get_network_config(struct orion_network_config *config) { return (int)orion_syscall1(ORION_SYS_GET_NET_CONFIG, (uint64_t)config); }
int orion_dhcp_renew(void) { return (int)orion_syscall0(ORION_SYS_DHCP_RENEW); }
int socket(int domain, int type, int protocol) { return (int)orion_syscall3(ORION_SYS_SOCKET, (uint64_t)domain, (uint64_t)type, (uint64_t)protocol); }
int bind(int descriptor, const struct sockaddr *address, socklen_t length) { return (int)orion_syscall3(ORION_SYS_BIND, (uint64_t)descriptor, (uint64_t)address, (uint64_t)length); }
int listen(int descriptor, int backlog) { return (int)orion_syscall2(ORION_SYS_LISTEN, (uint64_t)descriptor, (uint64_t)backlog); }
__attribute__((weak)) int accept(int descriptor, struct sockaddr *address, socklen_t *length) { return (int)orion_syscall3(ORION_SYS_ACCEPT, (uint64_t)descriptor, (uint64_t)address, (uint64_t)length); }
int connect(int descriptor, const struct sockaddr *address, socklen_t length) { return (int)orion_syscall3(ORION_SYS_CONNECT, (uint64_t)descriptor, (uint64_t)address, (uint64_t)length); }
ssize_t sendto(int descriptor, const void *buffer, size_t length, int flags, const struct sockaddr *destination, socklen_t destination_length) {
    if (destination && destination_length < sizeof(struct sockaddr_in)) return -1;
    return (ssize_t)orion_syscall5(ORION_SYS_SENDTO, (uint64_t)descriptor, (uint64_t)buffer, length, (uint64_t)flags, (uint64_t)destination);
}
ssize_t recvfrom(int descriptor, void *buffer, size_t length, int flags, struct sockaddr *source, socklen_t *source_length) {
    if (source && (!source_length || *source_length < sizeof(struct sockaddr_in))) return -1;
    ssize_t result = (ssize_t)orion_syscall5(ORION_SYS_RECVFROM, (uint64_t)descriptor, (uint64_t)buffer, length, (uint64_t)flags, (uint64_t)source);
    if (result >= 0 && source) *source_length = sizeof(struct sockaddr_in);
    return result;
}
ssize_t send(int descriptor, const void *buffer, size_t length, int flags) { return sendto(descriptor, buffer, length, flags, NULL, 0); }
ssize_t recv(int descriptor, void *buffer, size_t length, int flags) { return recvfrom(descriptor, buffer, length, flags, NULL, NULL); }
int shutdown(int descriptor, int how) { return (int)orion_syscall2(ORION_SYS_SHUTDOWN, (uint64_t)descriptor, (uint64_t)how); }
in_addr_t inet_addr(const char *text) {
    if (!text) return INADDR_NONE;
    uint8_t octets[4];
    const char *cursor = text;
    for (size_t i = 0; i < 4; ++i) {
        if (*cursor < '0' || *cursor > '9') return INADDR_NONE;
        uint32_t value = 0;
        size_t digits = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10 + (uint32_t)(*cursor - '0');
            if (value > 255 || ++digits > 3) return INADDR_NONE;
            ++cursor;
        }
        octets[i] = (uint8_t)value;
        if (i < 3) {
            if (*cursor != '.') return INADDR_NONE;
            ++cursor;
        }
    }
    if (*cursor) return INADDR_NONE;
    in_addr_t address;
    unsigned char *bytes = (unsigned char *)&address;
    for (size_t i = 0; i < 4; ++i) bytes[i] = octets[i];
    return address;
}
int stat(const char *path, struct stat *status) { return (int)orion_syscall2(ORION_SYS_STAT, (uint64_t)path, (uint64_t)status); }
int fstat(int descriptor, struct stat *status) { return (int)orion_syscall2(ORION_SYS_FSTAT, (uint64_t)descriptor, (uint64_t)status); }
int mkdir(const char *path, mode_t mode) { (void)mode; return (int)orion_syscall1(ORION_SYS_MKDIR, (uint64_t)path); }
int unlink(const char *path) { return (int)orion_syscall1(ORION_SYS_UNLINK, (uint64_t)path); }
void exit(int status) { (void)orion_syscall1(ORION_SYS_EXIT, (uint64_t)status); for (;;) __asm__ volatile("hlt"); }
size_t strlen(const char *text) { size_t length = 0; while (text[length]) ++length; return length; }
int strcmp(const char *left, const char *right) { while (*left && *left == *right) { ++left; ++right; } return (unsigned char)*left - (unsigned char)*right; }
int strncmp(const char *left, const char *right, size_t count) { for (size_t i = 0; i < count; ++i) { if (left[i] != right[i] || !left[i]) return (unsigned char)left[i] - (unsigned char)right[i]; } return 0; }
char *strcpy(char *destination, const char *source) { size_t i = 0; do { destination[i] = source[i]; } while (source[i++]); return destination; }
char *strncpy(char *destination, const char *source, size_t count) { size_t i = 0; for (; i < count && source[i]; ++i) destination[i] = source[i]; for (; i < count; ++i) destination[i] = 0; return destination; }
char *strcat(char *destination, const char *source) { strcpy(destination + strlen(destination), source); return destination; }
char *strchr(const char *text, int value) { while (*text) { if (*text == value) return (char *)text; ++text; } return value == 0 ? (char *)text : NULL; }
void *memcpy(void *destination, const void *source, size_t size) { unsigned char *to = destination; const unsigned char *from = source; for (size_t i = 0; i < size; ++i) to[i] = from[i]; return destination; }
void *memset(void *destination, int value, size_t size) { unsigned char *to = destination; for (size_t i = 0; i < size; ++i) to[i] = (unsigned char)value; return destination; }
long strtol(const char *text, char **end, int base) {
    while (*text == ' ' || *text == '\t' || *text == '\n') ++text;
    int negative = 0;
    if (*text == '-' || *text == '+') { negative = *text == '-'; ++text; }
    if (!base) base = text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 16 : text[0] == '0' ? 8 : 10;
    if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    long value = 0;
    const char *start = text;
    while (*text) {
        int digit = *text >= '0' && *text <= '9' ? *text - '0' : *text >= 'a' && *text <= 'z' ? *text - 'a' + 10 : *text >= 'A' && *text <= 'Z' ? *text - 'A' + 10 : base;
        if (digit >= base) break;
        value = value * base + digit;
        ++text;
    }
    if (end) *end = (char *)(text == start ? start : text);
    return negative ? -value : value;
}
int atoi(const char *text) { return (int)strtol(text, NULL, 10); }
int putchar(int value) { char character = (char)value; return write(STDOUT_FILENO, &character, 1) == 1 ? (unsigned char)character : -1; }
int puts(const char *text) { size_t length = strlen(text); if (write(STDOUT_FILENO, text, length) != (ssize_t)length) return -1; return putchar('\n') < 0 ? -1 : 0; }
int getchar(void) { char character; return read(STDIN_FILENO, &character, 1) == 1 ? (unsigned char)character : -1; }
char *fgets(char *buffer, int size, void *stream) {
    (void)stream;
    if (!buffer || size <= 0) return NULL;
    int count = 0;
    while (count + 1 < size) {
        int character = getchar();
        if (character < 0) break;
        buffer[count++] = (char)character;
        if (character == '\n') break;
    }
    if (!count) return NULL;
    buffer[count] = 0;
    return buffer;
}
static size_t number_text(char *output, uint64_t value, unsigned base, int negative) {
    char reverse[32];
    size_t count = 0;
    do { unsigned digit = (unsigned)(value % base); reverse[count++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10); value /= base; } while (value);
    size_t out = 0;
    if (negative) output[out++] = '-';
    while (count) output[out++] = reverse[--count];
    return out;
}
int printf(const char *format, ...) {
    __builtin_va_list arguments;
    __builtin_va_start(arguments, format);
    char output[512];
    size_t used = 0;
    for (size_t i = 0; format[i] && used < sizeof(output) - 1; ++i) {
        if (format[i] != '%') { output[used++] = format[i]; continue; }
        ++i;
        if (!format[i]) break;
        if (format[i] == '%') { output[used++] = '%'; continue; }
        int long_value = 0;
        int size_value = 0;
        if (format[i] == 'l') { long_value = 1; ++i; if (format[i] == 'l') ++i; }
        else if (format[i] == 'z') { size_value = 1; ++i; }
        char conversion = format[i];
        if (conversion == 's') {
            const char *text = __builtin_va_arg(arguments, const char *);
            if (!text) text = "(null)";
            while (*text && used < sizeof(output) - 1) output[used++] = *text++;
        } else if (conversion == 'c') {
            output[used++] = (char)__builtin_va_arg(arguments, int);
        } else if (conversion == 'd' || conversion == 'i') {
            int64_t value = long_value || size_value ? __builtin_va_arg(arguments, int64_t) : __builtin_va_arg(arguments, int);
            uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1 : (uint64_t)value;
            used += number_text(output + used, magnitude, 10, value < 0);
        } else if (conversion == 'u' || conversion == 'x' || conversion == 'p') {
            uint64_t value = conversion == 'p' ? (uint64_t)__builtin_va_arg(arguments, void *) : long_value || size_value ? __builtin_va_arg(arguments, uint64_t) : __builtin_va_arg(arguments, unsigned int);
            if (conversion == 'p') { output[used++] = '0'; output[used++] = 'x'; }
            used += number_text(output + used, value, conversion == 'x' || conversion == 'p' ? 16 : 10, 0);
        } else {
            output[used++] = '%';
            output[used++] = conversion;
        }
    }
    __builtin_va_end(arguments);
    write(STDOUT_FILENO, output, used);
    return (int)used;
}
