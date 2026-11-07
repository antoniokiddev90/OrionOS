#ifndef MM_VMM_H
#define MM_VMM_H

#include <stdint.h>

void     vmm_init(void);
int      vmm_map(uint64_t va, uint64_t pa, uint64_t size, uint64_t flags);
int      vmm_unmap(uint64_t va, uint64_t size);
uint64_t vmm_alloc_at(uint64_t va, uint64_t size, uint64_t flags);
uint64_t vmm_alloc(uint64_t size, uint64_t flags);
int      vmm_is_mapped(uint64_t va);

#endif
