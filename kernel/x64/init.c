#include "init.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "atkbd.h"

void x64_init(void) {
    gdt_init();
    idt_init();
    pic_init();
    pit_init(100);
    atkbd_init();
}
