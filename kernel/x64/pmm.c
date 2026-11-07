#include "pmm.h"
#include <stddef.h>

static uint8_t  bitmap[PMM_MAX_FRAMES / 8];
static uint64_t total_frames;
static uint64_t used_frames;
static uint64_t next_free;

static void bit_set(uint64_t i) { bitmap[i >> 3] |=  (uint8_t)(1u << (i & 7)); }
static void bit_clr(uint64_t i) { bitmap[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static int  bit_get(uint64_t i) { return (bitmap[i >> 3] >> (i & 7)) & 1; }

void pmm_init(struct pmm_region *regions, int count, uint64_t kernel_end) {
    for (uint64_t i = 0; i < PMM_MAX_FRAMES / 8; ++i) bitmap[i] = 0xFF;
    total_frames = 0;

    for (int r = 0; r < count; ++r) {
        uint64_t start = (regions[r].base + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
        uint64_t end   = (regions[r].base + regions[r].length) / PMM_FRAME_SIZE;
        for (uint64_t f = start; f < end; ++f) {
            if (f >= PMM_MAX_FRAMES) break;
            bit_clr(f);
            if (f + 1 > total_frames) total_frames = f + 1;
        }
    }

    uint64_t reserved = (kernel_end + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    if (reserved < 256) reserved = 256;
    for (uint64_t f = 0; f < reserved && f < PMM_MAX_FRAMES; ++f) bit_set(f);

    used_frames = reserved;
    next_free = reserved;
}

uint64_t pmm_alloc_frame(void) {
    for (uint64_t scanned = 0; scanned < total_frames; ++scanned) {
        uint64_t i = next_free;
        next_free = (next_free + 1) % total_frames;
        if (!bit_get(i)) {
            bit_set(i);
            ++used_frames;
            return i * PMM_FRAME_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint64_t frame) {
    uint64_t idx = frame / PMM_FRAME_SIZE;
    if (idx >= total_frames) return;
    if (bit_get(idx)) {
        bit_clr(idx);
        if (used_frames) --used_frames;
        if (idx < next_free) next_free = idx;
    }
}

uint64_t pmm_total_frames(void) { return total_frames; }
uint64_t pmm_used_frames(void)  { return used_frames;  }
