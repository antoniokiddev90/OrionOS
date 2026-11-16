#ifndef MM_USER_MMAP_H
#define MM_USER_MMAP_H
#include <stdint.h>
#define USER_MMAP_PROT_NONE 0x0
#define USER_MMAP_PROT_READ 0x1
#define USER_MMAP_PROT_WRITE 0x2
#define USER_MMAP_PROT_EXEC 0x4
#define USER_MMAP_MAP_SHARED 0x01
#define USER_MMAP_MAP_PRIVATE 0x02
#define USER_MMAP_MAP_FIXED 0x10
#define USER_MMAP_MAP_ANONYMOUS 0x20
int64_t user_mmap(uint64_t address, uint64_t length, uint64_t protection, uint64_t flags);
int64_t user_munmap(uint64_t address, uint64_t length);
#endif
