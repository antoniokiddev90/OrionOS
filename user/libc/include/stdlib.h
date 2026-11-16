#ifndef ORION_STDLIB_H
#define ORION_STDLIB_H
#include <stddef.h>
void exit(int status);
int atoi(const char *text);
long strtol(const char *text, char **end, int base);
void *malloc(size_t size);
void free(void *pointer);
#endif
