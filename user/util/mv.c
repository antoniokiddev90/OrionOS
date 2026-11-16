#include "common.h"
#include <sys/stat.h>
int main(int argc, char **argv) {
    if (argc != 3) { puts("usage: mv source destination"); return 1; }
    if (util_copy_file(argv[1], argv[2]) != 0 || unlink(argv[1]) != 0) { puts("mv: move failed"); return 1; }
    return 0;
}
