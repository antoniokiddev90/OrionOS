#include "kprintf.h"
#include "log.h"
#include <stdarg.h>
#include <stdint.h>

static void emit_repeat(char value, int count) {
    while (count-- > 0) log_putc(value);
}

static void emit_string(const char *text) {
    if (!text) text = "(null)";
    while (*text) log_putc(*text++);
}

static void emit_integer(uint64_t magnitude, unsigned base, int upper, int negative, int width, int zero_pad) {
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = upper ? upper_digits : lower_digits;
    char buf[64];
    int count = 0;
    do {
        buf[count++] = digits[magnitude % base];
        magnitude /= base;
    } while (magnitude);
    int total = count + (negative ? 1 : 0);
    if (!zero_pad) emit_repeat(' ', width - total);
    if (negative) log_putc('-');
    if (zero_pad) emit_repeat('0', width - total);
    while (count) log_putc(buf[--count]);
}

static void emit_signed(va_list *args, int is_long, int width, int zero_pad) {
    int64_t value = is_long ? va_arg(*args, int64_t) : (int64_t)va_arg(*args, int);
    int negative = value < 0;
    uint64_t magnitude = negative ? (uint64_t)0 - (uint64_t)value : (uint64_t)value;
    emit_integer(magnitude, 10, 0, negative, width, zero_pad);
}

static void emit_unsigned(va_list *args, unsigned base, int upper, int is_long, int width, int zero_pad) {
    uint64_t value = is_long ? va_arg(*args, uint64_t) : (uint64_t)va_arg(*args, unsigned int);
    emit_integer(value, base, upper, 0, width, zero_pad);
}

static void emit_pointer(va_list *args) {
    uint64_t value = (uint64_t)(uintptr_t)va_arg(*args, void *);
    log_putc('0');
    log_putc('x');
    emit_integer(value, 16, 0, 0, 16, 1);
}

void kprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    for (const char *p = format; *p; ++p) {
        if (*p != '%') {
            log_putc(*p);
            continue;
        }
        ++p;
        int zero_pad = 0;
        int width = 0;
        int is_long = 0;
        if (*p == '0') {
            zero_pad = 1;
            ++p;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }
        while (*p == 'l' || *p == 'z') {
            is_long = 1;
            ++p;
        }
        switch (*p) {
            case 'd':
            case 'i': emit_signed(&args, is_long, width, zero_pad); break;
            case 'u': emit_unsigned(&args, 10, 0, is_long, width, zero_pad); break;
            case 'x': emit_unsigned(&args, 16, 0, is_long, width, zero_pad); break;
            case 'X': emit_unsigned(&args, 16, 1, is_long, width, zero_pad); break;
            case 'p': emit_pointer(&args); break;
            case 's': emit_string(va_arg(args, const char *)); break;
            case 'c': log_putc((char)va_arg(args, int)); break;
            case '%': log_putc('%'); break;
            case '\0': goto done;
            default:
                log_putc('%');
                log_putc(*p);
                break;
        }
    }
done:
    va_end(args);
}
