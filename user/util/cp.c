#include "common.h"
int main(int argc, char **argv) {
    if (argc != 3) { puts("usage: cp source destination"); return 1; }
    if (util_copy_file(argv[1], argv[2]) != 0) { puts("cp: copy failed"); return 1; }
    return 0;
}
