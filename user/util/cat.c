#include "common.h"
int main(int argc, char **argv) {
    char buffer[512];
    if (argc == 1) {
        ssize_t count;
        while ((count = read(STDIN_FILENO, buffer, sizeof(buffer))) > 0) write(STDOUT_FILENO, buffer, (size_t)count);
        return count < 0;
    }
    int result = 0;
    for (int i = 1; i < argc; ++i) if (util_print_file(argv[i]) != 0) { printf("cat: cannot read %s\n", argv[i]); result = 1; }
    return result;
}
