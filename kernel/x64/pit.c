#include "pit.h"
#include "sched/task.h"

static volatile uint64_t ticks;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

void pit_init(uint32_t frequency) {
    if (frequency == 0 || frequency > 1193182) frequency = 100;
    uint16_t divisor = (uint16_t)(1193182 / frequency);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)divisor);
    outb(0x40, (uint8_t)(divisor >> 8));
}

void pit_irq(void) {
    ++ticks;
    sched_set_need_resched();
}

uint64_t pit_ticks(void) { return ticks; }
