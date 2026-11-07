#include "tss.h"
#include "gdt.h"
#include <stddef.h>

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss64 tss;

void tss_init(void) {
    uint8_t *p = (uint8_t *)&tss;
    for (size_t i = 0; i < sizeof(tss); ++i) p[i] = 0;
    tss.iomap_base = (uint16_t)sizeof(tss);
    gdt_set_tss((uint64_t)&tss, (uint32_t)(sizeof(tss) - 1));
}

void tss_set_rsp0(uint64_t rsp0) { tss.rsp0 = rsp0; }
uint64_t tss_get_rsp0(void) { return tss.rsp0; }
