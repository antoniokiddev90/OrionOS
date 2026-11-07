#include "gdt.h"
#include "tss.h"

struct __attribute__((packed)) gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
};

struct __attribute__((packed)) gdt_tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
};

struct __attribute__((packed)) gdtr {
    uint16_t limit;
    uint64_t base;
};

static struct {
    struct gdt_entry entries[5];
    struct gdt_tss_entry tss;
} gdt;

void gdt_set_tss(uint64_t base, uint32_t limit) {
    gdt.tss.limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt.tss.base_low    = (uint16_t)(base & 0xFFFF);
    gdt.tss.base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt.tss.access      = 0x89;
    gdt.tss.granularity = (uint8_t)((limit >> 16) & 0x0F);
    gdt.tss.base_high   = (uint8_t)((base >> 24) & 0xFF);
    gdt.tss.base_upper  = (uint32_t)(base >> 32);
    gdt.tss.reserved    = 0;
}

void gdt_init(void) {
    struct gdtr descriptor;
    gdt.entries[0] = (struct gdt_entry){0x0000, 0, 0, 0x00, 0x00, 0};
    gdt.entries[1] = (struct gdt_entry){0xFFFF, 0, 0, 0x9A, 0xAF, 0};
    gdt.entries[2] = (struct gdt_entry){0xFFFF, 0, 0, 0x92, 0xAF, 0};
    gdt.entries[3] = (struct gdt_entry){0xFFFF, 0, 0, 0xF2, 0xAF, 0};
    gdt.entries[4] = (struct gdt_entry){0xFFFF, 0, 0, 0xFA, 0xAF, 0};
    tss_init();
    descriptor.limit = sizeof(gdt) - 1;
    descriptor.base  = (uint64_t)&gdt;
    gdt_load((uint64_t *)&descriptor);
    __asm__ volatile("ltr %w0" : : "r"((uint16_t)GDT_TSS));
}
