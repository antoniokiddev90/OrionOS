#include "paging.h"
#include "pmm.h"
#include <stddef.h>

static uint64_t *kernel_pml4;

static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(uint64_t v) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

static inline uint64_t pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline uint64_t pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline uint64_t pd_index(uint64_t va)   { return (va >> 21) & 0x1FF; }
static inline uint64_t pt_index(uint64_t va)   { return (va >> 12) & 0x1FF; }

static uint64_t *alloc_table(void) {
    uint64_t pa = pmm_alloc_frame();
    if (!pa) return NULL;
    uint64_t *t = (uint64_t *)pa;
    for (int i = 0; i < 512; ++i) t[i] = 0;
    return t;
}

void paging_init(void) {
    kernel_pml4 = (uint64_t *)(read_cr3() & PTE_ADDR_MASK);
}

uint64_t paging_get_cr3(void) { return read_cr3(); }

void paging_set_cr3(uint64_t cr3) { write_cr3(cr3); }

void paging_invlpg(uint64_t va) {
    __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
}

uint64_t *paging_pml4(void) { return kernel_pml4; }

uint64_t *paging_walk(uint64_t va, int create) {
    uint64_t *pml4 = kernel_pml4;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_index(va)] & PTE_ADDR_MASK);
    if (!(pml4[pml4_index(va)] & PTE_PRESENT)) {
        if (!create) return NULL;
        pdpt = alloc_table();
        if (!pdpt) return NULL;
        pml4[pml4_index(va)] = (uint64_t)pdpt | PTE_PRESENT | PTE_WRITE;
    } else if ((pml4[pml4_index(va)] & PTE_HUGE)) {
        return NULL;
    }
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_index(va)] & PTE_ADDR_MASK);
    if (!(pdpt[pdpt_index(va)] & PTE_PRESENT)) {
        if (!create) return NULL;
        pd = alloc_table();
        if (!pd) return NULL;
        pdpt[pdpt_index(va)] = (uint64_t)pd | PTE_PRESENT | PTE_WRITE;
    } else if ((pdpt[pdpt_index(va)] & PTE_HUGE)) {
        return NULL;
    }
    uint64_t *pt = (uint64_t *)(pd[pd_index(va)] & PTE_ADDR_MASK);
    if (!(pd[pd_index(va)] & PTE_PRESENT)) {
        if (!create) return NULL;
        pt = alloc_table();
        if (!pt) return NULL;
        pd[pd_index(va)] = (uint64_t)pt | PTE_PRESENT | PTE_WRITE;
    } else if ((pd[pd_index(va)] & PTE_HUGE)) {
        return NULL;
    }
    return pt;
}

int paging_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t *pt = paging_walk(va, 1);
    if (!pt) return -1;
    pt[pt_index(va)] = (pa & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    paging_invlpg(va);
    return 0;
}

int paging_unmap_page(uint64_t va) {
    uint64_t *pt = paging_walk(va, 0);
    if (!pt) return -1;
    pt[pt_index(va)] = 0;
    paging_invlpg(va);
    return 0;
}

int paging_map_range(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags) {
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; ++i) {
        if (paging_map_page(va + i * PAGE_SIZE, pa + i * PAGE_SIZE, flags) != 0)
            return -1;
    }
    return 0;
}
int paging_map_user_page(uint64_t va, uint64_t pa) {
    uint64_t *pt = paging_walk(va, 1);
    if (!pt) return -1;

    uint64_t *pml4 = kernel_pml4;
    uint64_t pi = pml4_index(va);
    pml4[pi] |= PTE_USER;
    uint64_t *pdpt = (uint64_t *)(pml4[pi] & PTE_ADDR_MASK);

    uint64_t dpti = pdpt_index(va);
    pdpt[dpti] |= PTE_USER;
    uint64_t *pd = (uint64_t *)(pdpt[dpti] & PTE_ADDR_MASK);

    uint64_t pdi = pd_index(va);
    pd[pdi] |= PTE_USER;

    pt[pt_index(va)] = (pa & PTE_ADDR_MASK) | PTE_PRESENT | PTE_WRITE | PTE_USER;
    paging_invlpg(va);
    return 0;
}
