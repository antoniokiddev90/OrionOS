#include "x64/init.h"
#include "x64/framebuffer.h"
#include "x64/vga.h"
#include "x64/uart.h"
#include "log/kprintf.h"
#include "sched/task.h"
#include "exec/process.h"
#include "block/ata.h"
#include "x64/e1000.h"
#include "net/net.h"

extern uint8_t _kernel_end;

void kernel_main(uint32_t mb_magic, uint32_t mb_info) {
    __asm__ volatile("cli");
    uart_init();
    vga_capture_mode();
    vga_clear();
    x64_init();
    kprintf("OrionOS v0.07 x86_64\n");
    x64_mm_init(mb_magic, mb_info, (uint64_t)&_kernel_end);
    kprintf("mm ok\n");

    if (framebuffer_init(mb_magic, mb_info) == 0) {
        struct orion_fb_info info;
        if (framebuffer_get_info(&info) == 0)
            kprintf("GRUB framebuffer %ux%u, %u bpp, pitch %u\n", info.width, info.height,
                    info.bits_per_pixel, info.pitch);
    } else {
        vga_set_text_mode();
        kprintf("GRUB framebuffer unavailable; VGA text mode active\n");
    }

    if (e1000_init() != 0) kprintf("E1000 controller unavailable\n");
    else {
        kprintf("E1000 initialized\n");
        if (net_init() != 0) kprintf("Network stack unavailable\n");
        else kprintf("Network stack initialized\n");
    }

    task_init();
    kprintf("scheduler ok\n");
    if (ata_init() != 0) kprintf("ATA primary master unavailable\n");
    else if (process_start_init() != 0) kprintf("init unavailable\n");

    __asm__ volatile("sti");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
