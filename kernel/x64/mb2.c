#include "mb2.h"
#include <stddef.h>

int mb2_find_mmap(uint32_t magic, uint32_t info_ptr,
                  struct mb2_mmap_region *out, int max) {
    if (magic != MB2_BOOTLOADER_MAGIC) return 0;
    uint8_t *p = (uint8_t *)(uint64_t)info_ptr;
    uint32_t total = *(uint32_t *)p;
    uint8_t *end = p + total;
    p += 8;
    while (p < end) {
        struct mb2_tag *tag = (struct mb2_tag *)p;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_MMAP) {
            struct mb2_tag_mmap *mm = (struct mb2_tag_mmap *)p;
            uint8_t *e = p + sizeof(struct mb2_tag_mmap);
            uint8_t *e_end = p + mm->size;
            int count = 0;
            while (e < e_end && count < max) {
                struct mb2_mmap_entry *me = (struct mb2_mmap_entry *)e;
                if (me->type == MB2_MMAP_AVAILABLE && me->len > 0) {
                    out[count].base = me->addr;
                    out[count].length = me->len;
                    ++count;
                }
                e += mm->entry_size;
            }
            return count;
        }
        p += (tag->size + 7) & ~7u;
    }
    return 0;
}

int mb2_find_cmdline(uint32_t magic, uint32_t info_ptr,
                     char *out, int capacity) {
    if (magic != MB2_BOOTLOADER_MAGIC || !out || capacity <= 0) return 0;
    uint8_t *p = (uint8_t *)(uint64_t)info_ptr;
    uint32_t total = *(uint32_t *)p;
    uint8_t *end = p + total;
    p += 8;
    while (p < end) {
        struct mb2_tag *tag = (struct mb2_tag *)p;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_CMDLINE) {
            struct mb2_tag_string *s = (struct mb2_tag_string *)p;
            size_t length = tag->size > sizeof(struct mb2_tag_string)
                ? tag->size - sizeof(struct mb2_tag_string)
                : 0;
            int i = 0;
            while (i + 1 < capacity && (size_t)i < length && s->string[i]) {
                out[i] = s->string[i];
                ++i;
            }
            out[i] = 0;
            return i;
        }
        p += (tag->size + 7) & ~7u;
    }
    out[0] = 0;
    return 0;
}
