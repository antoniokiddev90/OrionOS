#include "vmm.h"
#include "../x64/paging.h"
#include "../x64/pmm.h"

void vmm_init(void) {
    paging_init();
}

int vmm_map(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags) {
    if (va & (PAGE_SIZE - 1)) return -1;
    if (pa & (PAGE_SIZE - 1)) return -1;
    return paging_map_range(va, pa, size, flags);
}

int vmm_unmap(uint64_t va, uint64_t size) {
    if (va & (PAGE_SIZE - 1)) return -1;
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; ++i) {
        if (paging_unmap_page(va + i * PAGE_SIZE) != 0) return -1;
    }
    return 0;
}

uint64_t vmm_alloc_at(uint64_t va, uint64_t size, uint64_t flags) {
    if (va & (PAGE_SIZE - 1)) return 0;
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; ++i) {
        uint64_t pa = pmm_alloc_frame();
        if (!pa) {
            vmm_unmap(va, i * PAGE_SIZE);
            return 0;
        }
        if (paging_map_page(va + i * PAGE_SIZE, pa, flags) != 0) {
            pmm_free_frame(pa);
            vmm_unmap(va, i * PAGE_SIZE);
            return 0;
        }
    }
    return va;
}

static uint64_t bump_next = 0xFFFFFE0000000000ULL;
static uint64_t bump_end  = 0xFFFFFE0000000000ULL + (256ULL * 1024 * 1024);
uint64_t vmm_alloc(uint64_t size, uint64_t flags) {
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t bytes = pages * PAGE_SIZE;
    if (bump_next + bytes > bump_end) return 0;
    uint64_t va = bump_next;
    bump_next += bytes;
    return vmm_alloc_at(va, bytes, flags);
}

int vmm_is_mapped(uint64_t va) {
    uint64_t *pt = paging_walk(va, 0);
    if (!pt) return 0;
    uint64_t e = pt[((va >> 12) & 0x1FF)];
    return (e & PTE_PRESENT) ? 1 : 0;
}
