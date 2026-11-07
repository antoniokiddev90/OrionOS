#ifndef X64_INIT_H
#define X64_INIT_H
#include <stdint.h>
void x64_init(void);
void x64_mm_init(uint32_t mb_magic, uint32_t mb_info, uint64_t kernel_end);
#endif
