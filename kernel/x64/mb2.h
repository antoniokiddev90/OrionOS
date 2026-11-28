#ifndef X64_MB2_H
#define X64_MB2_H
#include <stdint.h>
#define MB2_BOOTLOADER_MAGIC 0x36D76289
#define MB2_TAG_END 0
#define MB2_TAG_CMDLINE 1
#define MB2_TAG_MMAP 6
#define MB2_TAG_FRAMEBUFFER 8
#define MB2_MMAP_AVAILABLE 1
#define MB2_CMDLINE_MAX 256
struct mb2_tag {
    uint32_t type;
    uint32_t size;
};
struct mb2_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
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
struct mb2_framebuffer {
    uint64_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bits_per_pixel;
    uint8_t type;
    uint8_t red_field_position;
    uint8_t red_mask_size;
    uint8_t green_field_position;
    uint8_t green_mask_size;
    uint8_t blue_field_position;
    uint8_t blue_mask_size;
};
int mb2_find_mmap(uint32_t magic, uint32_t info_ptr, struct mb2_mmap_region *out, int max);
int mb2_find_cmdline(uint32_t magic, uint32_t info_ptr, char *out, int capacity);
int mb2_find_framebuffer(uint32_t magic, uint32_t info_ptr, struct mb2_framebuffer *out);
#endif
