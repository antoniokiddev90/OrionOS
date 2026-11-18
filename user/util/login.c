#include "common.h"

int main(void) {
    char name[32];
    char password[64];
    struct util_account account;

    printf("login: ");
    if (util_read_line(STDIN_FILENO, name, sizeof(name), UTIL_ECHO_ON) <= 0) return 1;
    printf("password: ");
    util_read_line(STDIN_FILENO, password, sizeof(password), UTIL_ECHO_OFF);

    if (util_find_account(name, 0, 1, &account) != 0 || util_check_password(name, password) != 0) {
        puts("login incorrect");
        return 1;
    }
    if (setgid(account.gid) != 0 || setuid(account.uid) != 0) {
        puts("login: cannot switch user");
        return 1;
    }
    chdir(account.home);
    char *arguments[] = {account.shell, NULL};
    execve(account.shell, arguments, NULL);
    puts("login: cannot execute shell");
    return 1;
}
