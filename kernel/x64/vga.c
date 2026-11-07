#include "vga.h"
static volatile uint16_t *const video = (volatile uint16_t *)0xB8000;
static uint8_t row, column;
static const uint8_t color = 0x0F;
static void scroll(void) {
    if (row < 25) return;
    for (uint8_t y = 1; y < 25; ++y) for (uint8_t x = 0; x < 80; ++x) video[(y - 1) * 80 + x] = video[y * 80 + x];
    for (uint8_t x = 0; x < 80; ++x) video[24 * 80 + x] = (uint16_t)color << 8 | ' ';
    row = 24;
}
void vga_clear(void) {
    for (uint16_t i = 0; i < 2000; ++i) video[i] = (uint16_t)color << 8 | ' ';
    row = 0; column = 0;
}
void vga_putc(char value) {
    if (value == '\n') { column = 0; ++row; scroll(); return; }
    if (value == '\r') { column = 0; return; }
    if (value == '\b') { if (column) --column; video[row * 80 + column] = (uint16_t)color << 8 | ' '; return; }
    video[row * 80 + column] = (uint16_t)color << 8 | (uint8_t)value;
    if (++column == 80) { column = 0; ++row; scroll(); }
}
void vga_write(const char *text) { while (*text) vga_putc(*text++); }
