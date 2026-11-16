#ifndef ORION_UTIL_COMMON_H
#define ORION_UTIL_COMMON_H
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
static int __attribute__((unused)) util_copy_file(const char *source, const char *destination) {
    int input = open(source, O_RDONLY);
    if (input < 0) return -1;
    int output = open(destination, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (output < 0) { close(input); return -1; }
    char buffer[512];
    int result = 0;
    ssize_t count;
    while ((count = read(input, buffer, sizeof(buffer))) > 0) {
        size_t sent = 0;
        while (sent < (size_t)count) {
            ssize_t written = write(output, buffer + sent, (size_t)count - sent);
            if (written <= 0) { result = -1; break; }
            sent += (size_t)written;
        }
        if (result) break;
    }
    if (count < 0) result = -1;
    close(input);
    close(output);
    return result;
}
static int __attribute__((unused)) util_print_file(const char *path) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    char buffer[256];
    ssize_t count;
    while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) write(STDOUT_FILENO, buffer, (size_t)count);
    close(descriptor);
    return count < 0 ? -1 : 0;
}
#endif
