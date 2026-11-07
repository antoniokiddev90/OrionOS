#ifndef X64_PIT_H
#define X64_PIT_H
#include <stdint.h>
void pit_init(uint32_t frequency);
void pit_irq(void);
uint64_t pit_ticks(void);
#endif
