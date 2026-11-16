#include "log.h"
#include "x64/uart.h"
#include "x64/vga.h"
void log_putc(char value) {
    if (value == '\n') uart_putc('\r');
    uart_putc(value);
    vga_putc(value);
}
