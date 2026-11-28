#ifndef X64_VGA_H
#define X64_VGA_H
#include <stdint.h>
void vga_capture_mode(void);
void vga_restore_mode(void);
void vga_set_text_mode(void);
void vga_clear(void);
void vga_write(const char *text);
void vga_putc(char value);
#endif
