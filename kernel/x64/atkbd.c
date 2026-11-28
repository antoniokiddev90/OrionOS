#include "atkbd.h"
#include "input_event.h"
#include "pit.h"
#include <stddef.h>
typedef struct {
    uint8_t *buffer;
    uint8_t tamanho;
    volatile uint8_t head;
    volatile uint8_t tail;
    volatile uint8_t shift_down;
    volatile uint8_t control_down;
    volatile uint8_t extended;
    volatile uint8_t e1_remaining;
} TecladoBuffer;
static uint8_t memoria_buffer[128];
static TecladoBuffer teclado;
static struct orion_input_event key_events[64];
static volatile uint8_t key_event_head;
static volatile uint8_t key_event_tail;
static const char keys[128] = { [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',[12]='-',[13]='=',[14]='\b',[15]='\t',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',[23]='i',[24]='o',[25]='p',[26]='[',[27]=']',[28]='\n',[30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',[39]=';',[40]='\'', [41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m',[51]=',',[52]='.',[53]='/',[57]=' ' };
static const char shifted[128] = { [2]='!',[3]='@',[4]='#',[5]='$',[6]='%',[7]='^',[8]='&',[9]='*',[10]='(',[11]=')',[12]='_',[13]='+',[16]='Q',[17]='W',[18]='E',[19]='R',[20]='T',[21]='Y',[22]='U',[23]='I',[24]='O',[25]='P',[26]='{',[27]='}',[30]='A',[31]='S',[32]='D',[33]='F',[34]='G',[35]='H',[36]='J',[37]='K',[38]='L',[39]=':',[40]='"',[41]='~',[43]='|',[44]='Z',[45]='X',[46]='C',[47]='V',[48]='B',[49]='N',[50]='M',[51]='<',[52]='>',[53]='?'};
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
void atkbd_init(void) {
    for (size_t i = 0; i < sizeof(memoria_buffer); ++i) memoria_buffer[i] = 0;
    teclado.buffer = memoria_buffer;
    teclado.tamanho = (uint8_t)sizeof(memoria_buffer);
    teclado.head = 0;
    teclado.tail = 0;
    teclado.shift_down = 0;
    teclado.control_down = 0;
    teclado.extended = 0;
    teclado.e1_remaining = 0;
    key_event_head = 0;
    key_event_tail = 0;

    while (inb(0x64) & 1) (void)inb(0x60);
    outb(0x64, 0xAE);
}
void atkbd_irq(void) {
    uint8_t scan = inb(0x60);
    if (scan == 0xFA || scan == 0xFE) return;
    if (scan == 0xE1) { teclado.e1_remaining = 2; return; }
    if (teclado.e1_remaining) { teclado.e1_remaining = (uint8_t)(teclado.e1_remaining - 1); return; }
    if (scan == 0xE0) { teclado.extended = 1; return; }
    if (!teclado.tamanho) return;
    uint8_t released = scan & 0x80;
    uint8_t code = scan & 0x7F;
    uint8_t extended = teclado.extended;
    teclado.extended = 0;
    uint8_t next_event = (uint8_t)((key_event_head + 1) % 64);
    if (next_event != key_event_tail) {
        struct orion_input_event *event = &key_events[key_event_head];
        event->ticks = pit_ticks();
        event->type = ORION_INPUT_EVENT_KEY;
        event->code = (uint16_t)(code | (extended ? 0x100 : 0));
        event->value = released ? 0 : 1;
        event->x = 0;
        event->y = 0;
        event->buttons = 0;
        event->reserved = 0;
        key_event_head = next_event;
    }
    if (code == 0x1D) { teclado.control_down = !released; return; }
    if (extended) return;
    if (code == 0x2A || code == 0x36) { teclado.shift_down = !released; return; }
    if (released) return;
    uint8_t ch = teclado.shift_down && shifted[code] ? (uint8_t)shifted[code] : (uint8_t)keys[code];
    if (teclado.control_down) {
        if (ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 'a' + 1);
        else if (ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch - 'A' + 1);
    }
    if (!ch) return;
    uint8_t next = (uint8_t)((teclado.head + 1) % teclado.tamanho);
    if (next != teclado.tail) { teclado.buffer[teclado.head] = ch; teclado.head = next; }
}
uint8_t atkbd_read_char(void) {
    if (!teclado.tamanho) return 0;
    if (teclado.tail == teclado.head) return 0;
    uint8_t ch = teclado.buffer[teclado.tail];
    teclado.buffer[teclado.tail] = 0;
    teclado.tail = (uint8_t)((teclado.tail + 1) % teclado.tamanho);
    return ch;
}
int atkbd_read_event(struct orion_input_event *event) {
    if (!event || key_event_tail == key_event_head) return 0;
    *event = key_events[key_event_tail];
    key_event_tail = (uint8_t)((key_event_tail + 1) % 64);
    return 1;
}
