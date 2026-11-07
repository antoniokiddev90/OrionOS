#include "atkbd.h"
#include "vga.h"
static volatile uint8_t buffer[128];
static volatile uint8_t head, tail;
static const char keys[128] = { [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',[12]='-',[13]='=',[14]='\b',[15]='\t',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\n',[30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',[39]=';',[40]='\'', [41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[57]=' ' };
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
void atkbd_init(void) { uint8_t status = inb(0x64); if (status & 1) (void)inb(0x60); outb(0x64, 0xAE); }
void atkbd_irq(void) {
    uint8_t scan = inb(0x60);
    if (scan & 0x80) return;
    uint8_t ch = keys[scan & 0x7F];
    if (!ch) return;
    uint8_t next = (uint8_t)((head + 1) & 127);
    if (next != tail) { buffer[head] = ch; head = next; }
}
uint8_t atkbd_read_char(void) {
    if (tail == head) return 0;
    uint8_t ch = buffer[tail]; tail = (uint8_t)((tail + 1) & 127);
    return ch;
}
