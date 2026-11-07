#ifndef X64_IDT_H
#define X64_IDT_H
#include <stdint.h>
void idt_init(void);
void idt_set_gate(uint8_t vector, void (*handler)(void));
#endif
