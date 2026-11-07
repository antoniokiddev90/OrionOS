#include "x64/init.h"
#include "x64/vga.h"
#include "x64/uart.h"
#include "x64/atkbd.h"
#include "mm/heap.h"

extern uint8_t _kernel_end;

void kernel_main(uint32_t mb_magic, uint32_t mb_info) {
    __asm__ volatile("cli");
    x64_init();
    vga_clear();
    vga_write("OrionOS v0.01 x86_64 Initialized\n");
    x64_mm_init(mb_magic, mb_info, (uint64_t)&_kernel_end);
    vga_write("paging/pmm/vmm/heap ok\n");
    uart_init();
    uart_write("OrionOS v0.01 x86_64 Initialized\n");
    __asm__ volatile("sti");

    for (;;) {
        uint8_t ch = atkbd_read_char();
        if (ch) vga_putc((char)ch);
        __asm__ volatile("sti; hlt");
    }
}
