#include "common.h"
int main(int argc, char **argv) {
    if (argc < 2) { puts("usage: touch file..."); return 1; }
    int result = 0;
    for (int i = 1; i < argc; ++i) {
        int descriptor = open(argv[i], O_CREAT | O_WRONLY, 0644);
        if (descriptor < 0) { printf("touch: cannot create %s\n", argv[i]); result = 1; }
        else close(descriptor);
    }
    return result;
}
