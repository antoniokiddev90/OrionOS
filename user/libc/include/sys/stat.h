#ifndef ORION_SYS_STAT_H
#define ORION_SYS_STAT_H
#include <stdint.h>
typedef uint32_t mode_t;
struct stat {
    uint64_t st_ino;
    uint64_t st_size;
    uint32_t st_mode;
    uint32_t st_reserved;
};
#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode) (((mode) & S_IFMT) == S_IFCHR)
int stat(const char *path, struct stat *status);
int fstat(int descriptor, struct stat *status);
int mkdir(const char *path, mode_t mode);
int unlink(const char *path);
#endif
