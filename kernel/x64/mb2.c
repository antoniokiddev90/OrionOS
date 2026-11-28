#include "mb2.h"
#include <stddef.h>
static int mb2_bounds(uint32_t magic, uint32_t info_ptr, uint8_t **start, uint8_t **end) {
    if (magic != MB2_BOOTLOADER_MAGIC || !info_ptr) return -1;
    uint8_t *base = (uint8_t *)(uint64_t)info_ptr;
    uint32_t total = *(uint32_t *)base;
    if (total < 16 || total > 16 * 1024 * 1024) return -1;
    *start = base + 8;
    *end = base + total;
    return 0;
}
int mb2_find_mmap(uint32_t magic, uint32_t info_ptr, struct mb2_mmap_region *out, int max) {
    uint8_t *p;
    uint8_t *end;
    if (!out || max <= 0 || mb2_bounds(magic, info_ptr, &p, &end) != 0) return 0;
    while (p + sizeof(struct mb2_tag) <= end) {
        struct mb2_tag *tag = (struct mb2_tag *)p;
        if (tag->size < sizeof(struct mb2_tag) || p + tag->size > end) return 0;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_MMAP) {
            if (tag->size < sizeof(struct mb2_tag_mmap)) return 0;
            struct mb2_tag_mmap *mm = (struct mb2_tag_mmap *)p;
            if (mm->entry_size < sizeof(struct mb2_mmap_entry) || !mm->entry_size) return 0;
            uint8_t *entry = p + sizeof(struct mb2_tag_mmap);
            uint8_t *entries_end = p + tag->size;
            int count = 0;
            while (entry + sizeof(struct mb2_mmap_entry) <= entries_end && count < max) {
                struct mb2_mmap_entry *region = (struct mb2_mmap_entry *)entry;
                if (region->type == MB2_MMAP_AVAILABLE && region->len) {
                    out[count].base = region->addr;
                    out[count].length = region->len;
                    ++count;
                }
                if (entry + mm->entry_size > entries_end) break;
                entry += mm->entry_size;
            }
            return count;
        }
        p += (tag->size + 7) & ~7u;
    }
    return 0;
}
int mb2_find_cmdline(uint32_t magic, uint32_t info_ptr, char *out, int capacity) {
    uint8_t *p;
    uint8_t *end;
    if (!out || capacity <= 0 || mb2_bounds(magic, info_ptr, &p, &end) != 0) return 0;
    while (p + sizeof(struct mb2_tag) <= end) {
        struct mb2_tag *tag = (struct mb2_tag *)p;
        if (tag->size < sizeof(struct mb2_tag) || p + tag->size > end) break;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_CMDLINE) {
            size_t length = tag->size > sizeof(struct mb2_tag_string)
                ? tag->size - sizeof(struct mb2_tag_string)
                : 0;
            int copied = 0;
            struct mb2_tag_string *source = (struct mb2_tag_string *)p;
            while (copied + 1 < capacity && (size_t)copied < length && source->string[copied]) {
                out[copied] = source->string[copied];
                ++copied;
            }
            out[copied] = 0;
            return copied;
        }
        p += (tag->size + 7) & ~7u;
    }
    out[0] = 0;
    return 0;
}
int mb2_find_framebuffer(uint32_t magic, uint32_t info_ptr, struct mb2_framebuffer *out) {
    uint8_t *p;
    uint8_t *end;
    if (!out || mb2_bounds(magic, info_ptr, &p, &end) != 0) return -1;
    while (p + sizeof(struct mb2_tag) <= end) {
        struct mb2_tag *tag = (struct mb2_tag *)p;
        if (tag->size < sizeof(struct mb2_tag) || p + tag->size > end) return -1;
        if (tag->type == MB2_TAG_END) return 0;
        if (tag->type == MB2_TAG_FRAMEBUFFER) {
            if (tag->size < 32) return -1;
            out->address = *(uint64_t *)(p + 8);
            out->pitch = *(uint32_t *)(p + 16);
            out->width = *(uint32_t *)(p + 20);
            out->height = *(uint32_t *)(p + 24);
            out->bits_per_pixel = p[28];
            out->type = p[29];
            out->red_field_position = 0;
            out->red_mask_size = 0;
            out->green_field_position = 0;
            out->green_mask_size = 0;
            out->blue_field_position = 0;
            out->blue_mask_size = 0;
            if (out->type == 1) {
                if (tag->size < 38) return -1;
                out->red_field_position = p[32];
                out->red_mask_size = p[33];
                out->green_field_position = p[34];
                out->green_mask_size = p[35];
                out->blue_field_position = p[36];
                out->blue_mask_size = p[37];
            }
            return 1;
        }
        p += (tag->size + 7) & ~7u;
    }
    return 0;
}
