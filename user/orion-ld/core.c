#include "ld.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void ld_fail(const char *message) {
    printf("orion-ld: %s\n", message);
    exit(1);
    for (;;) __asm__ volatile("hlt");
}

void *ld_grow(void *memory, size_t old_size, size_t new_size) {
    void *result = malloc(new_size ? new_size : 1);
    if (!result) ld_fail("out of memory");
    if (memory && old_size) memcpy(result, memory, old_size < new_size ? old_size : new_size);
    free(memory);
    return result;
}

char *ld_duplicate(const char *text) {
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (!copy) ld_fail("out of memory");
    memcpy(copy, text, length);
    return copy;
}

void ld_buffer_reserve(LdBuffer *buffer, size_t needed) {
    if (needed <= buffer->capacity) return;
    size_t capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2) ld_fail("output is too large");
        capacity *= 2;
    }
    buffer->data = ld_grow(buffer->data, buffer->capacity, capacity);
    buffer->capacity = capacity;
}

void ld_buffer_put(LdBuffer *buffer, const void *data, size_t size) {
    if (size > (size_t)-1 - buffer->size) ld_fail("output is too large");
    ld_buffer_reserve(buffer, buffer->size + size);
    if (size) memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
}

void ld_buffer_zero(LdBuffer *buffer, size_t size) {
    if (size > (size_t)-1 - buffer->size) ld_fail("output is too large");
    ld_buffer_reserve(buffer, buffer->size + size);
    if (size) memset(buffer->data + buffer->size, 0, size);
    buffer->size += size;
}

int ld_range_ok(size_t file_size, uint64_t offset, uint64_t length) {
    return offset <= file_size && length <= file_size - (size_t)offset;
}

const char *ld_string_at(const char *table, size_t size, uint32_t offset) {
    if (offset >= size) ld_fail("invalid string table offset");
    const char *text = table + offset;
    for (size_t i = offset; i < size; ++i) if (!table[i]) return text;
    ld_fail("unterminated string table entry");
    return "";
}

unsigned char *ld_read_file(const char *path, size_t *size) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) ld_fail("cannot open input");
    off_t end = lseek(descriptor, 0, SEEK_END);
    if (end < 0 || end > 16 * 1024 * 1024 || lseek(descriptor, 0, SEEK_SET) < 0) {
        close(descriptor);
        ld_fail("cannot read input");
    }
    unsigned char *data = malloc((size_t)end + 1);
    if (!data) {
        close(descriptor);
        ld_fail("out of memory");
    }
    size_t read_size = 0;
    while (read_size < (size_t)end) {
        size_t request = (size_t)end - read_size;
        if (request > 4096) request = 4096;
        ssize_t count = read(descriptor, data + read_size, request);
        if (count <= 0) {
            close(descriptor);
            free(data);
            ld_fail("cannot read input");
        }
        read_size += (size_t)count;
    }
    close(descriptor);
    data[read_size] = 0;
    *size = read_size;
    return data;
}

void ld_parse_arguments(Linker *linker, int argc, char **argv) {
    memset(linker, 0, sizeof(*linker));
    linker->output_path = "a.out";
    linker->entry_name = "_start";
    linker->base_address = 0x40000000ULL;
    size_t capacity = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            puts("orion-ld - native ELF64 static linker\nUsage: orion-ld [-T script] input.o ... [-o output]");
            exit(0);
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) ld_fail("-o requires an output path");
            linker->output_path = argv[i];
            continue;
        }
        if (strcmp(argv[i], "-T") == 0) {
            if (++i >= argc) ld_fail("-T requires a linker script");
            linker->script_path = argv[i];
            continue;
        }
        if (strncmp(argv[i], "-T", 2) == 0 && argv[i][2]) {
            linker->script_path = argv[i] + 2;
            continue;
        }
        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--entry") == 0) {
            if (++i >= argc) ld_fail("entry option requires a symbol");
            linker->entry_name = argv[i];
            continue;
        }
        if (strcmp(argv[i], "-nostdlib") == 0 || strcmp(argv[i], "-static") == 0 || strcmp(argv[i], "--no-undefined") == 0 || strcmp(argv[i], "-s") == 0) continue;
        if (strcmp(argv[i], "-z") == 0) {
            if (++i >= argc) ld_fail("-z requires an option");
            continue;
        }
        if (strcmp(argv[i], "-m") == 0) {
            if (++i >= argc || (strcmp(argv[i], "elf_x86_64") != 0 && strcmp(argv[i], "elf64-x86-64") != 0)) ld_fail("only elf_x86_64 is supported");
            continue;
        }
        if (argv[i][0] == '-') ld_fail("unsupported option");
        if (linker->input_count == capacity) {
            size_t next = capacity ? capacity * 2 : 8;
            linker->input_paths = ld_grow(linker->input_paths, capacity * sizeof(char *), next * sizeof(char *));
            capacity = next;
        }
        linker->input_paths[linker->input_count++] = argv[i];
    }
    if (!linker->input_count) ld_fail("no input object");
}

static char *find_text(char *text, const char *needle) {
    size_t length = strlen(needle);
    for (char *cursor = text; *cursor; ++cursor) if (strncmp(cursor, needle, length) == 0) return cursor;
    return NULL;
}

static uint64_t parse_address(char *text) {
    while (*text == ' ' || *text == '\t') ++text;
    char *end;
    long value = strtol(text, &end, 0);
    if (end == text || value < 0) ld_fail("invalid address in linker script");
    uint64_t result = (uint64_t)value;
    if (*end == 'K' || *end == 'k') result *= 1024;
    else if (*end == 'M' || *end == 'm') result *= 1024 * 1024;
    return result;
}

void ld_read_script(Linker *linker) {
    if (!linker->script_path) return;
    size_t size;
    char *text = (char *)ld_read_file(linker->script_path, &size);
    text[size] = 0;
    char *entry = find_text(text, "ENTRY(");
    if (entry) {
        entry += 6;
        char *end = strchr(entry, ')');
        if (!end) ld_fail("invalid ENTRY command");
        size_t entry_length = (size_t)(end - entry);
        char *entry_copy = malloc(entry_length + 1);
        if (!entry_copy) ld_fail("out of memory");
        memcpy(entry_copy, entry, entry_length);
        entry_copy[entry_length] = 0;
        char *start = entry_copy;
        while (*start == ' ' || *start == '\t') ++start;
        char *finish = start + strlen(start);
        while (finish > start && (finish[-1] == ' ' || finish[-1] == '\t')) *--finish = 0;
        if (!*start) ld_fail("entry symbol is missing");
        linker->entry_name = ld_duplicate(start);
        free(entry_copy);
    }
    char *location = find_text(text, ".");
    while (location) {
        char *cursor = location + 1;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '=') {
            linker->base_address = parse_address(cursor + 1);
            break;
        }
        location = find_text(location + 1, ".");
    }
    free(text);
}
