#include "paging.h"
#include "pmm.h"
#include <stddef.h>

static uint64_t *kernel_pml4;
static uint64_t *current_pml4;

static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

static uint64_t *alloc_table(void) {
    uint64_t physical = pmm_alloc_frame();
    if (!physical) return NULL;
    uint64_t *table = (uint64_t *)physical;
    for (size_t i = 0; i < 512; ++i) table[i] = 0;
    return table;
}

static uint64_t *walk_table(uint64_t *pml4, uint64_t va, int create) {
    uint64_t indices[4] = {(va >> 39) & 0x1ff, (va >> 30) & 0x1ff, (va >> 21) & 0x1ff, (va >> 12) & 0x1ff};
    uint64_t *table = pml4;
    for (size_t level = 0; level < 3; ++level) {
        uint64_t *entry = &table[indices[level]];
        if (!(*entry & PTE_PRESENT)) {
            if (!create) return NULL;
            uint64_t *next = alloc_table();
            if (!next) return NULL;
            *entry = (uint64_t)next | PTE_PRESENT | PTE_WRITE;
        } else if (*entry & PTE_HUGE) {
            return NULL;
        }
        table = (uint64_t *)(*entry & PTE_ADDR_MASK);
    }
    return table;
}

void paging_init(void) {
    kernel_pml4 = (uint64_t *)(read_cr3() & PTE_ADDR_MASK);
    current_pml4 = kernel_pml4;
    if (!(kernel_pml4[511] & PTE_PRESENT)) {
        uint64_t *pdpt = alloc_table();
        if (pdpt) kernel_pml4[511] = (uint64_t)pdpt | PTE_PRESENT | PTE_WRITE;
    }
}

uint64_t paging_get_cr3(void) { return read_cr3(); }
void paging_set_cr3(uint64_t cr3) { write_cr3(cr3); }
void paging_invlpg(uint64_t va) { __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory"); }
uint64_t *paging_pml4(void) { return current_pml4; }
uint64_t *paging_current_pml4(void) { return current_pml4; }

uint64_t *paging_walk_in(uint64_t *pml4, uint64_t va, int create) {
    if (!pml4) return NULL;
    return walk_table(pml4, va, create);
}

uint64_t *paging_walk(uint64_t va, int create) { return walk_table(current_pml4, va, create); }

int paging_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t *table = paging_walk(va, 1);
    if (!table) return -1;
    table[(va >> 12) & 0x1ff] = (pa & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    paging_invlpg(va);
    return 0;
}

int paging_unmap_page(uint64_t va) {
    uint64_t *table = paging_walk(va, 0);
    if (!table) return -1;
    table[(va >> 12) & 0x1ff] = 0;
    paging_invlpg(va);
    return 0;
}

int paging_map_range(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags) {
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; ++i) {
        if (paging_map_page(va + i * PAGE_SIZE, pa + i * PAGE_SIZE, flags) != 0) return -1;
    }
    return 0;
}

static int map_user_page(uint64_t *pml4, uint64_t va, uint64_t pa, uint64_t flags) {
    if (!pml4 || va < USER_SPACE_BASE || va >= USER_SPACE_END || (va & (PAGE_SIZE - 1))) return -1;
    uint64_t *table = walk_table(pml4, va, 1);
    if (!table) return -1;
    uint64_t indices[3] = {(va >> 39) & 0x1ff, (va >> 30) & 0x1ff, (va >> 21) & 0x1ff};
    uint64_t *level = pml4;
    for (size_t i = 0; i < 3; ++i) {
        level[indices[i]] |= PTE_USER;
        level = (uint64_t *)(level[indices[i]] & PTE_ADDR_MASK);
    }
    table[(va >> 12) & 0x1ff] = (pa & PTE_ADDR_MASK) | PTE_PRESENT | PTE_USER | flags;
    if (pml4 == current_pml4) paging_invlpg(va);
    return 0;
}

int paging_map_user_page(uint64_t va, uint64_t pa) { return map_user_page(current_pml4, va, pa, PTE_WRITE); }

int paging_map_user_page_in(uint64_t *pml4, uint64_t va, uint64_t pa) { return map_user_page(pml4, va, pa, PTE_WRITE); }

int paging_map_user_page_access_in(uint64_t *pml4, uint64_t va, uint64_t pa, int writable) {
    return map_user_page(pml4, va, pa, writable ? PTE_WRITE : 0);
}

int paging_unmap_user_page_in(uint64_t *pml4, uint64_t va, uint64_t *physical) {
    if (!pml4 || va < USER_SPACE_BASE || va >= USER_SPACE_END || (va & (PAGE_SIZE - 1))) return -1;
    uint64_t *table = walk_table(pml4, va, 0);
    if (!table) return 0;
    uint64_t *entry = &table[(va >> 12) & 0x1ff];
    if (!(*entry & PTE_PRESENT)) return 0;
    if (physical) *physical = *entry & PTE_ADDR_MASK;
    *entry = 0;
    if (pml4 == current_pml4) paging_invlpg(va);
    return 1;
}

uint64_t *paging_new_pml4(void) {
    uint64_t *pml4 = alloc_table();
    if (!pml4) return NULL;
    for (size_t i = 0; i < 512; ++i) pml4[i] = kernel_pml4[i];
    if (kernel_pml4[0] & PTE_PRESENT) {
        uint64_t *shared_pdpt = (uint64_t *)(kernel_pml4[0] & PTE_ADDR_MASK);
        uint64_t *private_pdpt = alloc_table();
        if (!private_pdpt) {
            pmm_free_frame((uint64_t)pml4);
            return NULL;
        }
        for (size_t i = 0; i < 512; ++i) private_pdpt[i] = shared_pdpt[i];
        pml4[0] = (uint64_t)private_pdpt | (kernel_pml4[0] & ~PTE_ADDR_MASK);
    }
    return pml4;
}

static void free_user_subtree(uint64_t *pdpt) {
    for (size_t i = 1; i < 512; ++i) {
        if (!(pdpt[i] & PTE_PRESENT) || (pdpt[i] & PTE_HUGE)) continue;
        uint64_t *pd = (uint64_t *)(pdpt[i] & PTE_ADDR_MASK);
        for (size_t j = 0; j < 512; ++j) {
            if (!(pd[j] & PTE_PRESENT) || (pd[j] & PTE_HUGE)) continue;
            uint64_t *pt = (uint64_t *)(pd[j] & PTE_ADDR_MASK);
            for (size_t k = 0; k < 512; ++k) {
                if (pt[k] & PTE_PRESENT) pmm_free_frame(pt[k] & PTE_ADDR_MASK);
            }
            pmm_free_frame((uint64_t)pt);
        }
        pmm_free_frame((uint64_t)pd);
    }
}

void paging_free_pml4(uint64_t *pml4) {
    if (!pml4 || pml4 == kernel_pml4) return;
    if (pml4[0] & PTE_PRESENT) {
        uint64_t *pdpt = (uint64_t *)(pml4[0] & PTE_ADDR_MASK);
        free_user_subtree(pdpt);
        pmm_free_frame((uint64_t)pdpt);
    }
    pmm_free_frame((uint64_t)pml4);
}

int paging_clone_user_pages(uint64_t *source, uint64_t *destination) {
    if (!source || !destination) return -1;
    for (uint64_t va = USER_SPACE_BASE; va < USER_SPACE_END; va += PAGE_SIZE) {
        uint64_t *source_table = walk_table(source, va, 0);
        if (!source_table) continue;
        uint64_t entry = source_table[(va >> 12) & 0x1ff];
        if (!(entry & PTE_PRESENT) || !(entry & PTE_USER)) continue;
        uint64_t physical = pmm_alloc_frame();
        if (!physical) return -1;
        uint8_t *to = (uint8_t *)physical;
        uint8_t *from = (uint8_t *)(entry & PTE_ADDR_MASK);
        for (size_t i = 0; i < PAGE_SIZE; ++i) to[i] = from[i];
        if (paging_map_user_page_access_in(destination, va, physical, (entry & PTE_WRITE) != 0) != 0) {
            pmm_free_frame(physical);
            return -1;
        }
    }
    return 0;
}

void paging_switch(uint64_t *pml4) {
    if (!pml4) return;
    current_pml4 = pml4;
    write_cr3((uint64_t)pml4);
}
