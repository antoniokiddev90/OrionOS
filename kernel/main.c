#include "x64/init.h"
#include "x64/vga.h"
#include "x64/uart.h"
#include "x64/atkbd.h"

void kernel_main(void) {
    __asm__ volatile("cli");
    x64_init();
    vga_clear();
    vga_write("OrionOS v0.01 x86_64 Initialized\n");
    uart_init();
    uart_write("OrionOS v0.01 x86_64 Initialized\n");
    __asm__ volatile("sti");
    for (;;) {
        uint8_t ch = atkbd_read_char();
        if (ch) vga_putc((char)ch);
        __asm__ volatile("sti; hlt");
    }
}
