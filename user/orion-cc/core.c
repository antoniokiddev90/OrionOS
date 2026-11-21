#include "compiler.h"

void fail(const char *message) {
    Token *token = compiler.position < compiler.token_count ? &compiler.tokens[compiler.position] : NULL;
    if (token && token->line) printf("orion-cc: %s:%u: %s\n", compiler.source_name, token->line, message);
    else printf("orion-cc: %s: %s\n", compiler.source_name ? compiler.source_name : "input", message);
    exit(1);
}

void *grow_memory(void *memory, size_t old_size, size_t new_size) {
    void *result = malloc(new_size ? new_size : 1);
    if (!result) fail("out of memory");
    if (memory && old_size) memcpy(result, memory, old_size < new_size ? old_size : new_size);
    free(memory);
    return result;
}

char *copy_range(const char *text, size_t length) {
    char *result = malloc(length + 1);
    if (!result) fail("out of memory");
    if (length) memcpy(result, text, length);
    result[length] = 0;
    return result;
}

char *copy_text(const char *text) {
    return copy_range(text, strlen(text));
}

void buffer_reserve(Buffer *buffer, size_t additional) {
    if (additional > (size_t)-1 - buffer->size) fail("output is too large");
    size_t required = buffer->size + additional;
    if (required <= buffer->capacity) return;
    size_t capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < required) {
        if (capacity > (size_t)-1 / 2) fail("output is too large");
        capacity *= 2;
    }
    buffer->data = grow_memory(buffer->data, buffer->capacity, capacity);
    buffer->capacity = capacity;
}

void buffer_bytes(Buffer *buffer, const char *data, size_t length) {
    buffer_reserve(buffer, length);
    if (length) memcpy(buffer->data + buffer->size, data, length);
    buffer->size += length;
}

void buffer_text(Buffer *buffer, const char *text) {
    buffer_bytes(buffer, text, strlen(text));
}

void buffer_char(Buffer *buffer, char value) {
    buffer_reserve(buffer, 1);
    buffer->data[buffer->size++] = value;
}

void buffer_number(Buffer *buffer, unsigned long long value) {
    char digits[32];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) buffer_char(buffer, digits[--count]);
}

void buffer_signed(Buffer *buffer, long long value) {
    if (value < 0) {
        buffer_char(buffer, '-');
        buffer_number(buffer, (unsigned long long)(-(value + 1)) + 1);
    } else buffer_number(buffer, (unsigned long long)value);
}

int is_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\v' || value == '\f';
}

int is_digit(char value) {
    return value >= '0' && value <= '9';
}

int is_alpha(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_';
}

int is_alnum(char value) {
    return is_alpha(value) || is_digit(value);
}

void *array_grow(void *memory, size_t *capacity, size_t need, size_t element_size) {
    if (need <= *capacity) return memory;
    size_t next = *capacity ? *capacity * 2 : 16;
    while (next < need) {
        if (next > (size_t)-1 / 2) fail("input is too large");
        next *= 2;
    }
    memory = grow_memory(memory, *capacity * element_size, next * element_size);
    *capacity = next;
    return memory;
}

size_t align_value(size_t value, size_t alignment) {
    size_t remainder = value % alignment;
    return remainder ? value + alignment - remainder : value;
}


Compiler compiler;
Function *active_function;
