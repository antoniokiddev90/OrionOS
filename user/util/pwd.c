#include "common.h"
int main(void) {
    char path[256];
    if (!getcwd(path, sizeof(path))) { puts("pwd: failed"); return 1; }
    puts(path);
    return 0;
}
