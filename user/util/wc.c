#include "common.h"
int main(int argc, char **argv) {
    if (argc != 2) { puts("usage: wc file"); return 1; }
    int descriptor = open(argv[1], O_RDONLY, 0);
    if (descriptor < 0) { puts("wc: cannot open file"); return 1; }
    char buffer[256];
    uint64_t lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    ssize_t count;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) {
        bytes += (uint64_t)count;
        for (ssize_t i = 0; i < count; ++i) {
            char c = buffer[i];
            if (c == '\n') ++lines;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') in_word = 0;
            else if (!in_word) { ++words; in_word = 1; }
        }
    }
    close(descriptor);
    printf("%lu %lu %lu %s\n", lines, words, bytes, argv[1]);
    return count < 0;
}
