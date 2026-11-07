#ifndef X64_MB2_H
#define X64_MB2_H

#include <stdint.h>

#define MB2_BOOTLOADER_MAGIC    0x36D76289
#define MB2_TAG_END             0
#define MB2_TAG_MMAP            6
#define MB2_MMAP_AVAILABLE      1

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
};

struct mb2_mmap_region {
    uint64_t base;
    uint64_t length;
};

int mb2_find_mmap(uint32_t magic, uint32_t info_ptr,
                  struct mb2_mmap_region *out, int max);

#endif
