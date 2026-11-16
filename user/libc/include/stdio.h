#ifndef ORION_STDIO_H
#define ORION_STDIO_H
#include <stddef.h>
int printf(const char *format, ...);
int puts(const char *text);
int putchar(int value);
int getchar(void);
char *fgets(char *buffer, int size, void *stream);
#endif
