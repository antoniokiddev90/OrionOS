#ifndef LOG_KPRINTF_H
#define LOG_KPRINTF_H
void kprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));
#endif
