#ifndef ORION_STRING_H
#define ORION_STRING_H
#include <stddef.h>
size_t strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, size_t count);
char *strcat(char *destination, const char *source);
char *strchr(const char *text, int value);
void *memcpy(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);
#endif
