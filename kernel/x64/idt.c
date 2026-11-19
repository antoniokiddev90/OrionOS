#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "atkbd.h"
#include "tss.h"
#include "log/kprintf.h"

#define IDT_ENTRIES        256
#define IRQ_VECTOR_BASE    32
#define IRQ_VECTOR_COUNT   16
#define EXCEPTION_COUNT    32
#define IDT_INTERRUPT_GATE 0x8E
#define KERNEL_CS          0x08
#define DF_IST             1

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

static struct idt_entry idt[IDT_ENTRIES];
static uint8_t df_ist_stack[4096] __attribute__((aligned(16)));

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
extern void exc1(void);
extern void exc2(void);
extern void exc3(void);
extern void exc4(void);
extern void exc5(void);
extern void exc6(void);
extern void exc7(void);
extern void exc8(void);
extern void exc9(void);
extern void exc10(void);
extern void exc11(void);
extern void exc12(void);
extern void exc13(void);
extern void exc14(void);
extern void exc15(void);
extern void exc16(void);
extern void exc17(void);
extern void exc18(void);
extern void exc19(void);
extern void exc20(void);
extern void exc21(void);
extern void exc22(void);
extern void exc23(void);
extern void exc24(void);
extern void exc25(void);
extern void exc26(void);
extern void exc27(void);
extern void exc28(void);
extern void exc29(void);
extern void exc30(void);
extern void exc31(void);

struct exc_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t exc_num, error_code;
    uint64_t rip, cs, rflags;
};

static void idt_set_gate_full(uint8_t v, void (*handler)(void), uint8_t dpl, uint8_t ist) {
    uint64_t address = (uint64_t)handler;
    uint8_t flags = (uint8_t)(IDT_INTERRUPT_GATE | ((dpl & 3) << 5));
    idt[v] = (struct idt_entry){
        (uint16_t)address, KERNEL_CS, ist, flags,
        (uint16_t)(address >> 16), (uint32_t)(address >> 32), 0
    };
}

void idt_set_gate(uint8_t v, void (*handler)(void)) {
    idt_set_gate_full(v, handler, 0, 0);
}

void idt_set_gate_dpl(uint8_t v, void (*handler)(void), uint8_t dpl) {
    idt_set_gate_full(v, handler, dpl, 0);
}

void idt_set_gate_ist(uint8_t v, void (*handler)(void), uint8_t ist) {
    idt_set_gate_full(v, handler, 0, ist);
}
void atkbd_init(void) {
    teclado.buffer = memoria_buffer;
    teclado.tamanho = 128;
    teclado.head = 0;
    teclado.tail = 0;
    teclado.shift_down = 0;
    teclado.control_down = 0;

    uint8_t status = inb(0x64); if (status & 1) (void)inb(0x60); outb(0x64, 0xAE);
}
void atkbd_irq(void) {
    uint8_t scan = inb(0x60);
    uint8_t released = scan & 0x80;
    uint8_t code = scan & 0x7F;
    if (code == 0x2A || code == 0x36) { teclado.shift_down = !released; return; }
    if (code == 0x1D) { teclado.control_down = !released; return; }
    if (released) return;
    uint8_t ch = teclado.shift_down && shifted[code] ? (uint8_t)shifted[code] : (uint8_t)keys[code];
    if (teclado.control_down && ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 'a' + 1);
    if (!ch) return;
    uint8_t next = (uint8_t)((teclado.head + 1) % teclado.tamanho);
    if (next != teclado.tail) { teclado.buffer[teclado.head] = ch; teclado.head = next; }
}
uint8_t atkbd_read_char(void) {
    if (teclado.tail == teclado.head) return 0;
    uint8_t ch = teclado.buffer[teclado.tail]; teclado.tail = (uint8_t)((teclado.tail + 1) % teclado.tamanho);
    return ch;
}
 
