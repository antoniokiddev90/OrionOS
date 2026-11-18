#include "common.h"
int main(void) {
    uid_t uid = geteuid();
    struct util_account account;
    if (util_find_account(NULL, uid, 0, &account) == 0) puts(account.name);
    else printf("%u\n", (unsigned int)uid);
    return 0;
}
