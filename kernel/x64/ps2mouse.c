#include "ps2mouse.h"
#include "pic.h"
#include "pit.h"
#include <stddef.h>
#define MOUSE_QUEUE_SIZE 64
static struct orion_input_event queue[MOUSE_QUEUE_SIZE];
static volatile uint8_t queue_head;
static volatile uint8_t queue_tail;
static uint8_t packet[3];
static uint8_t packet_index;
static uint32_t current_buttons;
static int initialized;
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static int wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000; ++i) if (!(inb(0x64) & 2)) return 0;
    return -1;
}
static int wait_output_full(void) {
    for (uint32_t i = 0; i < 100000; ++i) if (inb(0x64) & 1) return 0;
    return -1;
}
static int controller_command(uint8_t value) {
    if (wait_input_clear() != 0) return -1;
    outb(0x64, value);
    return 0;
}
static int controller_data(uint8_t value) {
    if (wait_input_clear() != 0) return -1;
    outb(0x60, value);
    return 0;
}
static int mouse_command(uint8_t value) {
    if (controller_command(0xD4) != 0 || controller_data(value) != 0 || wait_output_full() != 0) return -1;
    return inb(0x60) == 0xFA ? 0 : -1;
}
static void enqueue(int32_t dx, int32_t dy, uint32_t buttons) {
    uint8_t next = (uint8_t)((queue_head + 1) % MOUSE_QUEUE_SIZE);
    if (next == queue_tail) return;
    struct orion_input_event *event = &queue[queue_head];
    event->ticks = pit_ticks();
    event->type = ORION_INPUT_EVENT_MOUSE;
    event->code = 0;
    event->value = 0;
    event->x = dx;
    event->y = dy;
    event->buttons = buttons;
    event->reserved = 0;
    queue_head = next;
}
void ps2mouse_init(void) {
    queue_head = 0;
    queue_tail = 0;
    packet_index = 0;
    current_buttons = 0;
    initialized = 0;
    if (controller_command(0xA8) != 0 || controller_command(0x20) != 0 || wait_output_full() != 0) return;
    uint8_t command_byte = inb(0x60);
    command_byte = (uint8_t)((command_byte | 2) & ~0x20);
    if (controller_command(0x60) != 0 || controller_data(command_byte) != 0) return;
    if (mouse_command(0xF6) != 0 || mouse_command(0xF4) != 0) return;
    initialized = 1;
    pic_enable_irq(12);
}
void ps2mouse_irq(void) {
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;
    uint8_t value = inb(0x60);
    if (!(status & 0x20) || !initialized) return;
    if (!packet_index && !(value & 8)) return;
    packet[packet_index++] = value;
    if (packet_index < 3) return;
    packet_index = 0;
    int32_t dx = (int8_t)packet[1];
    int32_t dy = -(int32_t)(int8_t)packet[2];
    if (packet[0] & 0x40) dx = 0;
    if (packet[0] & 0x80) dy = 0;
    uint32_t buttons = (uint32_t)(packet[0] & 7);
    if (dx || dy || buttons != current_buttons) enqueue(dx, dy, buttons);
    current_buttons = buttons;
}
int ps2mouse_read_event(struct orion_input_event *event) {
    if (!event || queue_tail == queue_head) return 0;
    *event = queue[queue_tail];
    queue_tail = (uint8_t)((queue_tail + 1) % MOUSE_QUEUE_SIZE);
    return 1;
}
int ps2mouse_available(void) { return initialized; }
