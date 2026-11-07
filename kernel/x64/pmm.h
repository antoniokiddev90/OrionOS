#ifndef X64_PMM_H
#define X64_PMM_H

#include <stdint.h>

#define PMM_FRAME_SIZE   4096ULL
#define PMM_MAX_REGIONS  32
#define PMM_MAX_FRAMES   (4ULL * 1024 * 1024 * 1024 / PMM_FRAME_SIZE)

struct pmm_region {
    uint64_t base;
    uint64_t length;
};

void     pmm_init(struct pmm_region *regions, int count, uint64_t kernel_end);
uint64_t pmm_alloc_frame(void);
void     pmm_free_frame(uint64_t frame);
uint64_t pmm_total_frames(void);
uint64_t pmm_used_frames(void);

#endif
