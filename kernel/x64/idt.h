#ifndef X64_IDT_H
#define X64_IDT_H

#include <stdint.h>

void idt_init(void);
void idt_set_gate(uint8_t vector, void (*handler)(void));
void idt_set_gate_dpl(uint8_t vector, void (*handler)(void), uint8_t dpl);

#endif
