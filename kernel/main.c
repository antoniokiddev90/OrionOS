#include "x64/init.h"
#include "x64/vga.h"
#include "x64/uart.h"
#include "log/kprintf.h"
#include "sched/task.h"
#include "exec/process.h"
#include "block/ata.h"

extern uint8_t _kernel_end;

void kernel_main(uint32_t mb_magic, uint32_t mb_info) {
    __asm__ volatile("cli");
    uart_init();
    vga_clear();
    x64_init();
    kprintf("OrionOS v0.04 x86_64\n");
    x64_mm_init(mb_magic, mb_info, (uint64_t)&_kernel_end);
    kprintf("mm ok\n");

    task_init();
    kprintf("scheduler ok\n");
    if (ata_init() != 0) kprintf("ATA primary master unavailable\n");
    else if (process_start_init() != 0) kprintf("init unavailable\n");

    __asm__ volatile("sti");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
