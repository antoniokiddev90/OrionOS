#include "syscall.h"
#include "vga.h"
#include "idt.h"
#include "sched/task.h"
#include <stdint.h>
#include <stddef.h>

extern void syscall_entry(void);

#define SYS_WRITE  1
#define SYS_YIELD  2
#define SYS_EXIT   3

static uint64_t sys_write(int fd, const char *buf, size_t len) {
    (void)fd;
    for (size_t i = 0; i < len; ++i) vga_putc(buf[i]);
    return len;
}

static uint64_t sys_yield(void) {
    sched_set_need_resched();
    return 0;
}

static uint64_t sys_exit(int code) {
    (void)code;
    task_exit();
    return 0;
}

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a4;
    (void)a5;
    switch (num) {
        case SYS_WRITE: return sys_write((int)a1, (const char *)a2, (size_t)a3);
        case SYS_YIELD: return sys_yield();
        case SYS_EXIT:  return sys_exit((int)a1);
        default:        return (uint64_t)-1;
    }
}

void syscall_init(void) {
    idt_set_gate_dpl(0x9E, syscall_entry, 3);
}
