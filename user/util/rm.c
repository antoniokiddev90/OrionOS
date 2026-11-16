#include "common.h"
#include <sys/stat.h>
int main(int argc, char **argv) {
    if (argc < 2) { puts("usage: rm file..."); return 1; }
    int result = 0;
    for (int i = 1; i < argc; ++i) if (unlink(argv[i]) != 0) { printf("rm: cannot remove %s\n", argv[i]); result = 1; }
    return result;
}
