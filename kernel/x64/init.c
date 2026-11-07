#include "init.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "atkbd.h"
#include "pmm.h"
#include "mb2.h"
#include "../mm/vmm.h"
#include "../mm/heap.h"

void x64_init(void) {
    gdt_init();
    idt_init();
    pic_init();
    pit_init(100);
    atkbd_init();
}

void x64_mm_init(uint32_t mb_magic, uint32_t mb_info, uint64_t kernel_end) {
    struct mb2_mmap_region regions[PMM_MAX_REGIONS];
    struct pmm_region pregions[PMM_MAX_REGIONS];

    int n = mb2_find_mmap(mb_magic, mb_info, regions, PMM_MAX_REGIONS);
    if (n <= 0) {
        pregions[0].base = 0;
        pregions[0].length = 128ULL * 1024 * 1024;
        n = 1;
    } else {
        for (int i = 0; i < n; ++i) {
            pregions[i].base = regions[i].base;
            pregions[i].length = regions[i].length;
        }
    }

    pmm_init(pregions, n, kernel_end);
    vmm_init();
    heap_init();
}
