#include "vga.h"
static volatile uint16_t *const video = (volatile uint16_t *)0xB8000;
static uint8_t row;
static uint8_t column;
static const uint8_t color = 0x0F;
static uint8_t saved_seq[5];
static uint8_t saved_crtc[25];
static uint8_t saved_gc[9];
static uint8_t saved_attr[21];
static uint8_t saved_misc;
static uint16_t saved_crtc_port;
static uint16_t saved_status_port;
static int saved_mode;
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static void indexed_write(uint16_t index_port, uint16_t data_port, uint8_t index, uint8_t value) {
    outb(index_port, index);
    outb(data_port, value);
}
static uint8_t indexed_read(uint16_t index_port, uint16_t data_port, uint8_t index) {
    outb(index_port, index);
    return inb(data_port);
}
static uint8_t read_attribute(uint8_t index, uint16_t status_port) {
    (void)inb(status_port);
    outb(0x3C0, index);
    return inb(0x3C1);
}
static void write_attribute(uint8_t index, uint8_t value, uint16_t status_port) {
    (void)inb(status_port);
    outb(0x3C0, index);
    outb(0x3C0, value);
}
void vga_capture_mode(void) {
    saved_misc = inb(0x3CC);
    saved_crtc_port = (saved_misc & 1) ? 0x3D4 : 0x3B4;
    saved_status_port = (saved_misc & 1) ? 0x3DA : 0x3BA;
    for (uint8_t i = 0; i < 5; ++i) saved_seq[i] = indexed_read(0x3C4, 0x3C5, i);
    for (uint8_t i = 0; i < 25; ++i) saved_crtc[i] = indexed_read(saved_crtc_port, (uint16_t)(saved_crtc_port + 1), i);
    for (uint8_t i = 0; i < 9; ++i) saved_gc[i] = indexed_read(0x3CE, 0x3CF, i);
    for (uint8_t i = 0; i < 21; ++i) saved_attr[i] = read_attribute(i, saved_status_port);
    (void)inb(saved_status_port);
    outb(0x3C0, 0x20);
    saved_mode = 1;
}
void vga_restore_mode(void) {
    if (!saved_mode) return;
    outb(0x3C2, saved_misc);
    indexed_write(0x3C4, 0x3C5, 0, 0x01);
    for (uint8_t i = 1; i < 5; ++i) indexed_write(0x3C4, 0x3C5, i, saved_seq[i]);
    indexed_write(0x3C4, 0x3C5, 0, saved_seq[0]);
    indexed_write(saved_crtc_port, (uint16_t)(saved_crtc_port + 1), 0x11,
                  (uint8_t)(saved_crtc[0x11] & 0x7F));
    for (uint8_t i = 0; i < 25; ++i) {
        if (i == 0x11) continue;
        indexed_write(saved_crtc_port, (uint16_t)(saved_crtc_port + 1), i, saved_crtc[i]);
    }
    indexed_write(saved_crtc_port, (uint16_t)(saved_crtc_port + 1), 0x11, saved_crtc[0x11]);
    for (uint8_t i = 0; i < 9; ++i) indexed_write(0x3CE, 0x3CF, i, saved_gc[i]);
    for (uint8_t i = 0; i < 21; ++i) write_attribute(i, saved_attr[i], saved_status_port);
    (void)inb(saved_status_port);
    outb(0x3C0, 0x20);
}
void vga_set_text_mode(void) {
    static const uint8_t sequencer[5] = {0x03, 0x00, 0x03, 0x00, 0x02};
    static const uint8_t crtc[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
        0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF
    };
    static const uint8_t graphics[9] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF};
    static const uint8_t attributes[21] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x0C, 0x00, 0x0F, 0x08, 0x00
    };
    outb(0x3C2, 0x67);
    indexed_write(0x3C4, 0x3C5, 0, 0x01);
    for (uint8_t i = 1; i < 5; ++i) indexed_write(0x3C4, 0x3C5, i, sequencer[i]);
    indexed_write(0x3C4, 0x3C5, 0, sequencer[0]);
    indexed_write(0x3D4, 0x3D5, 0x11,
                  (uint8_t)(indexed_read(0x3D4, 0x3D5, 0x11) & 0x7F));
    for (uint8_t i = 0; i < 25; ++i) indexed_write(0x3D4, 0x3D5, i, crtc[i]);
    for (uint8_t i = 0; i < 9; ++i) indexed_write(0x3CE, 0x3CF, i, graphics[i]);
    for (uint8_t i = 0; i < 21; ++i) write_attribute(i, attributes[i], 0x3DA);
    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
}
static void scroll(void) {
    if (row < 25) return;
    for (uint8_t y = 1; y < 25; ++y) {
        for (uint8_t x = 0; x < 80; ++x) video[(y - 1) * 80 + x] = video[y * 80 + x];
    }
    for (uint8_t x = 0; x < 80; ++x) video[24 * 80 + x] = (uint16_t)color << 8 | ' ';
    row = 24;
}
void vga_clear(void) {
    for (uint16_t i = 0; i < 2000; ++i) video[i] = (uint16_t)color << 8 | ' ';
    row = 0;
    column = 0;
}
void vga_putc(char value) {
    if (value == '\n') {
        column = 0;
        ++row;
        scroll();
        return;
    }
    if (value == '\r') {
        column = 0;
        return;
    }
    if (value == '\b') {
        if (column) --column;
        video[row * 80 + column] = (uint16_t)color << 8 | ' ';
        return;
    }
    video[row * 80 + column] = (uint16_t)color << 8 | (uint8_t)value;
    if (++column == 80) {
        column = 0;
        ++row;
        scroll();
    }
}
void vga_write(const char *text) { while (*text) vga_putc(*text++); }
