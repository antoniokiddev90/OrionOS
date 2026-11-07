#ifndef X64_GDT_H
#define X64_GDT_H

#include <stdint.h>

#define GDT_KERNEL_CS 0x08
#define GDT_KERNEL_DS 0x10
#define GDT_USER_DS   0x18
#define GDT_USER_CS   0x20
#define GDT_TSS       0x28

void gdt_init(void);
void gdt_load(uint64_t *descriptor);
void gdt_set_tss(uint64_t base, uint32_t limit);

#endif
