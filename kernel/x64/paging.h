#ifndef X64_PAGING_H
#define X64_PAGING_H

#include <stdint.h>

#define PAGE_SIZE       4096ULL
#define PAGE_MASK       (~(PAGE_SIZE - 1ULL))

#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITE       (1ULL << 1)
#define PTE_USER        (1ULL << 2)
#define PTE_PWT         (1ULL << 3)
#define PTE_PCD         (1ULL << 4)
#define PTE_ACCESSED    (1ULL << 5)
#define PTE_DIRTY       (1ULL << 6)
#define PTE_HUGE        (1ULL << 7)
#define PTE_GLOBAL      (1ULL << 8)
#define PTE_NX          (1ULL << 63)

#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

void     paging_init(void);
uint64_t paging_get_cr3(void);
void     paging_set_cr3(uint64_t cr3);
void     paging_invlpg(uint64_t va);
uint64_t *paging_new_pml4(void);
void      paging_free_pml4(uint64_t *pml4);
uint64_t *paging_current_pml4(void);
void      paging_switch(uint64_t *pml4);
uint64_t *paging_pml4(void);
uint64_t *paging_walk(uint64_t va, int create);
int      paging_map_page(uint64_t va, uint64_t pa, uint64_t flags);
int      paging_unmap_page(uint64_t va);
int      paging_map_range(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags);
int paging_map_user_page(uint64_t va, uint64_t pa);
#endif
