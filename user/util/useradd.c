#include "common.h"
#include <sys/stat.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        puts("usage: useradd name");
        return 1;
    }
    if (geteuid() != 0) {
        puts("useradd: permission denied");
        return 1;
    }
    const char *name = argv[1];
    size_t length = strlen(name);
    if (!length || length > 31 || strchr(name, ':') || strchr(name, '/')) {
        puts("useradd: invalid user name");
        return 1;
    }
    struct util_account existing;
    if (util_find_account(name, 0, 1, &existing) == 0) {
        puts("useradd: user already exists");
        return 1;
    }
    char first[64];
    char second[64];
    printf("new password: ");
    util_read_line(STDIN_FILENO, first, sizeof(first), UTIL_ECHO_OFF);
    putchar('\n');
    printf("retype new password: ");
    util_read_line(STDIN_FILENO, second, sizeof(second), UTIL_ECHO_OFF);
    putchar('\n');
    if (strcmp(first, second) != 0) {
        puts("useradd: passwords do not match");
        return 1;
    }
    char entry[128];
    if (util_make_entry(second, entry, sizeof(entry)) != 0) {
        puts("useradd: cannot hash password");
        return 1;
    }
    uint32_t uid = util_next_uid();
    char uid_text[16];
    util_format_uint(uid, uid_text);
    char line[256];
    line[0] = 0;
    strcat(line, name);
    strcat(line, ":x:");
    strcat(line, uid_text);
    strcat(line, ":");
    strcat(line, uid_text);
    strcat(line, ":");
    strcat(line, name);
    strcat(line, ":/home/");
    strcat(line, name);
    strcat(line, ":/bin/sh\n");
    int descriptor = open("/etc/passwd", O_WRONLY | O_APPEND, 0);
    if (descriptor < 0 || write(descriptor, line, strlen(line)) != (ssize_t)strlen(line)) {
        if (descriptor >= 0) close(descriptor);
        puts("useradd: cannot update passwd");
        return 1;
    }
    close(descriptor);
    if (util_shadow_store(name, entry) != 0) {
        puts("useradd: cannot update shadow");
        return 1;
    }
    char home[64];
    home[0] = 0;
    strcat(home, "/home/");
    strcat(home, name);
    if (mkdir(home, 0755) != 0 || chown(home, uid, uid) != 0) {
        puts("useradd: cannot create home directory");
        return 1;
    }
    puts("useradd: user created");
    return 0;
}
