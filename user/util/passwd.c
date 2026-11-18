#include "common.h"

int main(int argc, char **argv) {
    struct util_account self;
    if (util_find_account(NULL, getuid(), 0, &self) != 0) {
        puts("passwd: unknown user");
        return 1;
    }
    const char *target = argc > 1 ? argv[1] : self.name;
    int is_root = geteuid() == 0;
    if (strcmp(target, self.name) != 0 && !is_root) {
        puts("passwd: permission denied");
        return 1;
    }
    struct util_account account;
    if (util_find_account(target, 0, 1, &account) != 0) {
        puts("passwd: unknown user");
        return 1;
    }
    if (!is_root) {
        char current[64];
        printf("current password: ");
        util_read_line(STDIN_FILENO, current, sizeof(current), UTIL_ECHO_OFF);
        putchar('\n');
        if (util_check_password(self.name, current) != 0) {
            puts("passwd: authentication failure");
            return 1;
        }
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
        puts("passwd: passwords do not match");
        return 1;
    }
    char entry[128];
    if (util_make_entry(second, entry, sizeof(entry)) != 0 || util_shadow_store(target, entry) != 0) {
        puts("passwd: cannot update shadow");
        return 1;
    }
    puts("passwd: password updated");
    return 0;
}
