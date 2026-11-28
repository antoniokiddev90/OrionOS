#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "atkbd.h"
#include "ps2mouse.h"
#include "tss.h"
#include "log/kprintf.h"

#define IDT_ENTRIES        256
#define IRQ_VECTOR_BASE    32
#define IRQ_VECTOR_COUNT   16
#define EXCEPTION_COUNT    32
#define IDT_INTERRUPT_GATE 0x8E
#define KERNEL_CS          0x08
#define DF_VECTOR          8
#define DF_IST             1
#define PF_VECTOR          14

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

void idt_init(void) {
    void (*const irq_handlers[IRQ_VECTOR_COUNT])(void) = {
        irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7,
        irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    };
    void (*const exc_handlers[EXCEPTION_COUNT])(void) = {
        exc0,  exc1,  exc2,  exc3,  exc4,  exc5,  exc6,  exc7,
        exc8,  exc9,  exc10, exc11, exc12, exc13, exc14, exc15,
        exc16, exc17, exc18, exc19, exc20, exc21, exc22, exc23,
        exc24, exc25, exc26, exc27, exc28, exc29, exc30, exc31
    };
    struct idtr descriptor;

    for (unsigned i = 0; i < IDT_ENTRIES; ++i) idt[i] = (struct idt_entry){0};
    for (unsigned i = 0; i < IRQ_VECTOR_COUNT; ++i)
        idt_set_gate((uint8_t)(IRQ_VECTOR_BASE + i), irq_handlers[i]);
    for (unsigned i = 0; i < EXCEPTION_COUNT; ++i)
        idt_set_gate((uint8_t)i, exc_handlers[i]);

    tss_set_ist(DF_IST, (uint64_t)(df_ist_stack + sizeof(df_ist_stack)));
    idt_set_gate_ist(DF_VECTOR, exc8, DF_IST);

    descriptor.limit = sizeof(idt) - 1;
    descriptor.base  = (uint64_t)idt;
    __asm__ volatile("lidt %0" : : "m"(descriptor));
}

void exception_dispatch(struct exc_frame *f) {
    static const char *const names[EXCEPTION_COUNT] = {
        "#DE",  "#DB",  "NMI",  "#BP",  "#OF",  "#BR",  "#UD",  "#NM",
        "#DF",  "?",    "#TS",  "#NP",  "#SS",  "#GP",  "#PF",  "?",
        "#MF",  "#AC",  "#MC",  "#XM",  "#VE",  "#CP",  "?",    "?",
        "?",    "?",    "?",    "?",    "#HV",  "#VC",  "#SX",  "?"
    };
    const char *name = f->exc_num < EXCEPTION_COUNT ? names[f->exc_num] : "?";

    kprintf("\nEXCEPTION %s err=%p rip=%p", name, (void *)f->error_code, (void *)f->rip);
    if (f->exc_num == PF_VECTOR) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf(" cr2=%p", (void *)cr2);
    }
    kprintf("\n");
}

void irq_dispatch(uint64_t irq) {
    if (irq == 0) pit_irq();
    if (irq == 1) atkbd_irq();
    if (irq == 12) ps2mouse_irq();
    pic_eoi((uint8_t)irq);
}
