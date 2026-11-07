#ifndef X64_GDT_H
#define X64_GDT_H
#include <stdint.h>
void gdt_init(void);
void gdt_load(uint64_t *descriptor);
#endif
