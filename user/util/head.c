#include "common.h"
int main(int argc, char **argv) {
    if (argc != 2) { puts("usage: head file"); return 1; }
    int descriptor = open(argv[1], O_RDONLY, 0);
    if (descriptor < 0) { puts("head: cannot open file"); return 1; }
    char buffer[256];
    int lines = 0;
    ssize_t count;
    while (lines < 10 && (count = read(descriptor, buffer, sizeof(buffer))) > 0) {
        for (ssize_t i = 0; i < count && lines < 10; ++i) {
            putchar(buffer[i]);
            if (buffer[i] == '\n') ++lines;
        }
    }
    close(descriptor);
    return count < 0;
}
