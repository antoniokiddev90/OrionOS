#include "common.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("usage: sudo command [arguments...]");
        return 1;
    }
    struct util_account self;
    if (util_find_account(NULL, getuid(), 0, &self) != 0) {
        puts("sudo: unknown user");
        return 1;
    }
    if (!util_in_sudoers(self.name)) {
        printf("%s is not in the sudoers file\n", self.name);
        return 1;
    }
    char password[64];
    printf("[sudo] password for %s: ", self.name);
    util_read_line(STDIN_FILENO, password, sizeof(password), UTIL_ECHO_OFF);
    putchar('\n');
    if (util_check_password(self.name, password) != 0) {
        puts("sudo: authentication failure");
        return 1;
    }
    if (setgid(0) != 0 || setuid(0) != 0) {
        puts("sudo: cannot become root");
        return 1;
    }
    char path[256];
    path[0] = 0;
    if (!strchr(argv[1], '/')) strcat(path, "/bin/");
    strcat(path, argv[1]);
    execve(path, argv + 1, NULL);
    printf("sudo: command not found: %s\n", argv[1]);
    return 1;
}
