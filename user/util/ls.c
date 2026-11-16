#include "common.h"
#include <sys/dirent.h>
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : ".";
    int descriptor = open(path, O_RDONLY, 0);
    if (descriptor < 0) { printf("ls: cannot open %s\n", path); return 1; }
    char buffer[1024];
    int count;
    while ((count = getdents64(descriptor, buffer, sizeof(buffer))) > 0) {
        size_t position = 0;
        while (position < (size_t)count) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + position);
            if (!entry->d_reclen) break;
            printf("%s%s  ", entry->d_name, entry->d_type == DT_DIR ? "/" : "");
            position += entry->d_reclen;
        }
    }
    putchar('\n');
    close(descriptor);
    return count < 0;
}
