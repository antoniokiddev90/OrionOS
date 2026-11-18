#include "common.h"
#include <sys/dirent.h>

int main(int argc, char **argv) {
    int show_all = 0;
    int one_per_line = 0;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-a") == 0) show_all = 1;
        else if (strcmp(argv[i], "-1") == 0) one_per_line = 1;
        else path = argv[i];
    }
    if (!path) path = ".";

    int descriptor = open(path, O_RDONLY, 0);
    if (descriptor < 0) { printf("ls: cannot open %s\n", path); return 1; }

    char buffer[1024];
    int count;
    while ((count = getdents64(descriptor, buffer, sizeof(buffer))) > 0) {
        size_t position = 0;
        while (position < (size_t)count) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + position);
            if (!entry->d_reclen) break;
            if (!show_all && (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)) {
                position += entry->d_reclen;
                continue;
            }
            if (one_per_line)
                printf("%s%s\n", entry->d_name, entry->d_type == DT_DIR ? "/" : "");
            else
                printf("%s%s  ", entry->d_name, entry->d_type == DT_DIR ? "/" : "");
            position += entry->d_reclen;
        }
    }
    if (!one_per_line) putchar('\n');
    close(descriptor);
    return count < 0;
}
