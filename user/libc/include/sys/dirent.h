#ifndef ORION_SYS_DIRENT_H
#define ORION_SYS_DIRENT_H
#include <stdint.h>
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[];
};
#define DT_UNKNOWN 0
#define DT_REG 8
#define DT_DIR 4
#endif
