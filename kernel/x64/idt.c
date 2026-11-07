#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "atkbd.h"
struct __attribute__((packed)) idt_entry { uint16_t low; uint16_t selector; uint8_t ist; uint8_t flags; uint16_t middle; uint32_t high; uint32_t reserved; };
struct __attribute__((packed)) idtr { uint16_t limit; uint64_t base; };
static struct idt_entry idt[256];
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);
void idt_set_gate(uint8_t v, void (*handler)(void)) {
    uint64_t address = (uint64_t)handler;
    idt[v] = (struct idt_entry){(uint16_t)address, 0x08, 0, 0x8E, (uint16_t)(address >> 16), (uint32_t)(address >> 32), 0};
}
void idt_init(void) {
    struct idtr descriptor;
    void (*const handlers[16])(void) = {irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,irq8,irq9,irq10,irq11,irq12,irq13,irq14,irq15};
    for (unsigned i = 0; i < 256; ++i) idt[i] = (struct idt_entry){0};
    for (unsigned i = 0; i < 16; ++i) idt_set_gate((uint8_t)(32 + i), handlers[i]);
    descriptor.limit = sizeof(idt) - 1;
    descriptor.base = (uint64_t)idt;
    __asm__ volatile("lidt %0" : : "m"(descriptor));
}
void irq_dispatch(uint64_t irq) {
    if (irq == 0) pit_irq();
    if (irq == 1) atkbd_irq();
    pic_eoi((uint8_t)irq);
}
