#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "atkbd.h"
#include "vga.h"

struct __attribute__((packed)) idt_entry {
    uint16_t low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t middle;
    uint32_t high;
    uint32_t reserved;
};

struct __attribute__((packed)) idtr {
    uint16_t limit;
    uint64_t base;
};

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

extern void exc0(void);
extern void exc6(void);
extern void exc8(void);
extern void exc13(void);
extern void exc14(void);

struct exc_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t exc_num, error_code;
    uint64_t rip, cs, rflags;
};

void idt_set_gate_dpl(uint8_t v, void (*handler)(void), uint8_t dpl) {
    uint64_t address = (uint64_t)handler;
    uint8_t flags = (uint8_t)(0x8E | ((dpl & 3) << 5));
    idt[v] = (struct idt_entry){
        (uint16_t)address, 0x08, 0, flags,
        (uint16_t)(address >> 16), (uint32_t)(address >> 32), 0
    };
}

void idt_set_gate(uint8_t v, void (*handler)(void)) {
    idt_set_gate_dpl(v, handler, 0);
}

void idt_init(void) {
    struct idtr descriptor;
    void (*const handlers[16])(void) = {
        irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7,
        irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    };
    for (unsigned i = 0; i < 256; ++i) idt[i] = (struct idt_entry){0};
    for (unsigned i = 0; i < 16; ++i) idt_set_gate((uint8_t)(32 + i), handlers[i]);
    idt_set_gate(0, exc0);
    idt_set_gate(6, exc6);
    idt_set_gate(8, exc8);
    idt_set_gate(13, exc13);
    idt_set_gate(14, exc14);
    descriptor.limit = sizeof(idt) - 1;
    descriptor.base  = (uint64_t)idt;
    __asm__ volatile("lidt %0" : : "m"(descriptor));
}

static const char hexc[] = "0123456789ABCDEF";

static void print_hex(uint64_t v) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; ++i) buf[2 + i] = hexc[(v >> (60 - i * 4)) & 0xF];
    buf[18] = 0;
    vga_write(buf);
}

void exception_dispatch(struct exc_frame *f) {
    vga_write("\nEXCEPTION ");
    switch (f->exc_num) {
        case 0:  vga_write("#DE"); break;
        case 6:  vga_write("#UD"); break;
        case 8:  vga_write("#DF"); break;
        case 13: vga_write("#GP"); break;
        case 14: vga_write("#PF"); break;
        default: vga_write("?");   break;
    }
    vga_write(" err=");
    print_hex(f->error_code);
    vga_write(" rip=");
    print_hex(f->rip);
    if (f->exc_num == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        vga_write(" cr2=");
        print_hex(cr2);
    }
    vga_write("\n");
}

void irq_dispatch(uint64_t irq) {
    if (irq == 0) pit_irq();
    if (irq == 1) atkbd_irq();
    pic_eoi((uint8_t)irq);
}
