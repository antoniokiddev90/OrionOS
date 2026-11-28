#ifndef X64_PIC_H
#define X64_PIC_H
#include <stdint.h>
void pic_init(void);
void pic_eoi(uint8_t irq);
void pic_enable_irq(uint8_t irq);
#endif
