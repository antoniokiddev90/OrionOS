#include "pic.h"
static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static void wait_io(void) { outb(0x80, 0); }
void pic_init(void) {
    uint8_t master = inb(0x21), slave = inb(0xA1);
    outb(0x20, 0x11); wait_io(); outb(0xA0, 0x11); wait_io();
    outb(0x21, 0x20); wait_io(); outb(0xA1, 0x28); wait_io();
    outb(0x21, 0x04); wait_io(); outb(0xA1, 0x02); wait_io();
    outb(0x21, 0x01); wait_io(); outb(0xA1, 0x01); wait_io();
    outb(0x21, (uint8_t)(master & 0xFC)); outb(0xA1, (uint8_t)(slave & 0xFF));
}
void pic_eoi(uint8_t irq) { if (irq >= 8) outb(0xA0, 0x20); outb(0x20, 0x20); }
