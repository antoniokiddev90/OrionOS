#include "x64/init.h"
#include "x64/vga.h"
#include "x64/uart.h"
#include "sched/task.h"
#include "user/user.h"

extern uint8_t _kernel_end;

void kernel_main(uint32_t mb_magic, uint32_t mb_info) {
    __asm__ volatile("cli");
    x64_init();
    vga_clear();
    vga_write("OrionOS v0.03 x86_64\n");
    x64_mm_init(mb_magic, mb_info, (uint64_t)&_kernel_end);
    vga_write("mm ok\n");
    uart_init();
    uart_write("OrionOS v0.03 x86_64\n");

    task_init();
    user_demo_init();
    vga_write("scheduler ok\n");

    __asm__ volatile("sti");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
