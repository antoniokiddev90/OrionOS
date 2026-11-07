#include "gdt.h"
struct __attribute__((packed)) gdt_entry { uint16_t limit_low; uint16_t base_low; uint8_t base_middle; uint8_t access; uint8_t granularity; uint8_t base_high; };
struct __attribute__((packed)) tss_entry { uint16_t limit_low; uint16_t base_low; uint8_t base_middle; uint8_t access; uint8_t granularity; uint8_t base_high; uint32_t base_upper; uint32_t reserved; };
struct __attribute__((packed)) gdtr { uint16_t limit; uint64_t base; };
static struct { struct gdt_entry entries[3]; struct tss_entry tss; } gdt;
void gdt_init(void) {
    struct gdtr descriptor;
    gdt.entries[0] = (struct gdt_entry){0};
    gdt.entries[1] = (struct gdt_entry){0,0,0,0x9A,0xA0,0};
    gdt.entries[2] = (struct gdt_entry){0,0,0,0x92,0xA0,0};
    gdt.tss = (struct tss_entry){0};
    descriptor.limit = sizeof(gdt) - 1;
    descriptor.base = (uint64_t)&gdt;
    gdt_load((uint64_t *)&descriptor);
}
