#include "as.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AS_SHT_PROGBITS 1
#define AS_SHT_NOBITS 8
#define AS_SHF_WRITE 1
#define AS_SHF_ALLOC 2
#define AS_SHF_EXEC 4

void as_fail(const char *message) {
    printf("orion-as: %s\n", message);
    exit(2);
    for (;;) __asm__ volatile("hlt");
}

void *as_grow(void *memory, size_t old_size, size_t new_size) {
    void *result = malloc(new_size ? new_size : 1);
    if (!result) as_fail("out of memory");
    if (memory && old_size) memcpy(result, memory, old_size < new_size ? old_size : new_size);
    free(memory);
    return result;
}

char *as_duplicate(const char *text) {
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (!copy) as_fail("out of memory");
    memcpy(copy, text, length);
    return copy;
}

void as_buffer_reserve(AsBuffer *buffer, size_t additional) {
    if (additional > (size_t)-1 - buffer->size) as_fail("source is too large");
    size_t needed = buffer->size + additional;
    if (needed <= buffer->capacity) return;
    size_t capacity = buffer->capacity ? buffer->capacity : 256;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2) as_fail("source is too large");
        capacity *= 2;
    }
    buffer->data = as_grow(buffer->data, buffer->capacity, capacity);
    buffer->capacity = capacity;
}

static int section_is_bss(const Assembler *assembler, uint32_t index) {
    return index < assembler->section_count && assembler->sections[index].type == AS_SHT_NOBITS;
}

void as_emit_byte(Assembler *assembler, unsigned char value) {
    AsSection *section = &assembler->sections[assembler->current_section];
    if (section_is_bss(assembler, assembler->current_section)) {
        if (section->bss_size == (uint64_t)-1) as_fail("section is too large");
        ++section->bss_size;
        return;
    }
    as_buffer_reserve(&section->bytes, 1);
    section->bytes.data[section->bytes.size++] = value;
}

void as_emit_integer(Assembler *assembler, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) as_emit_byte(assembler, (unsigned char)(value >> (i * 8)));
}

void as_emit_zeros(Assembler *assembler, size_t count) {
    for (size_t i = 0; i < count; ++i) as_emit_byte(assembler, 0);
}

static uint64_t section_position(const Assembler *assembler, uint32_t index) {
    const AsSection *section = &assembler->sections[index];
    return section->type == AS_SHT_NOBITS ? section->bss_size : section->bytes.size;
}

void as_align(Assembler *assembler, uint64_t alignment) {
    if (!alignment || (alignment & (alignment - 1))) as_fail("alignment must be a power of two");
    AsSection *section = &assembler->sections[assembler->current_section];
    if (alignment > section->alignment) section->alignment = alignment;
    uint64_t position = section_position(assembler, assembler->current_section);
    uint64_t padding = (alignment - position % alignment) % alignment;
    if (padding > 0x100000) as_fail("alignment padding is too large");
    as_emit_zeros(assembler, (size_t)padding);
}

int as_section_find(Assembler *assembler, const char *name, int create) {
    for (size_t i = 0; i < assembler->section_count; ++i) {
        if (strcmp(assembler->sections[i].name, name) == 0) return (int)i;
    }
    if (!create) return -1;
    if (assembler->section_count == assembler->section_capacity) {
        size_t capacity = assembler->section_capacity ? assembler->section_capacity * 2 : 8;
        assembler->sections = as_grow(assembler->sections, assembler->section_capacity * sizeof(AsSection), capacity * sizeof(AsSection));
        assembler->section_capacity = capacity;
    }
    AsSection *section = &assembler->sections[assembler->section_count];
    memset(section, 0, sizeof(*section));
    section->name = as_duplicate(name);
    section->type = AS_SHT_PROGBITS;
    section->alignment = 1;
    section->flags = AS_SHF_ALLOC;
    if (strcmp(name, ".text") == 0 || strcmp(name, ".multiboot") == 0 || strncmp(name, ".text.", 6) == 0) {
        section->flags |= AS_SHF_EXEC;
        section->alignment = 16;
    } else if (strcmp(name, ".rodata") == 0 || strncmp(name, ".rodata.", 8) == 0) {
        section->alignment = 8;
    } else {
        section->flags |= AS_SHF_WRITE;
        section->alignment = 8;
    }
    if (strcmp(name, ".bss") == 0 || strncmp(name, ".bss.", 5) == 0) section->type = AS_SHT_NOBITS;
    return (int)assembler->section_count++;
}

int as_symbol_find(Assembler *assembler, const char *name, int create) {
    for (size_t i = 0; i < assembler->symbol_count; ++i) {
        if (strcmp(assembler->symbols[i].name, name) == 0) return (int)i;
    }
    if (!create) return -1;
    if (assembler->symbol_count == assembler->symbol_capacity) {
        size_t capacity = assembler->symbol_capacity ? assembler->symbol_capacity * 2 : 32;
        assembler->symbols = as_grow(assembler->symbols, assembler->symbol_capacity * sizeof(AsSymbol), capacity * sizeof(AsSymbol));
        assembler->symbol_capacity = capacity;
    }
    AsSymbol *symbol = &assembler->symbols[assembler->symbol_count];
    memset(symbol, 0, sizeof(*symbol));
    symbol->name = as_duplicate(name);
    symbol->section = 0;
    return (int)assembler->symbol_count++;
}

int as_symbol_define(Assembler *assembler, const char *name) {
    char qualified[256];
    if (name[0] == '.') {
        if (!assembler->local_scope) as_fail("local label has no parent label");
        size_t scope_length = strlen(assembler->local_scope);
        size_t name_length = strlen(name);
        if (scope_length + name_length + 1 > sizeof(qualified)) as_fail("symbol name is too long");
        memcpy(qualified, assembler->local_scope, scope_length);
        memcpy(qualified + scope_length, name, name_length + 1);
        name = qualified;
    } else {
        free(assembler->local_scope);
        assembler->local_scope = as_duplicate(name);
    }
    int index = as_symbol_find(assembler, name, 1);
    AsSymbol *symbol = &assembler->symbols[index];
    uint64_t position = section_position(assembler, assembler->current_section);
    if (assembler->pass == 1) {
        if (symbol->defined) as_fail("duplicate symbol");
        symbol->defined = 1;
        symbol->section = (uint16_t)(assembler->current_section + 1);
        symbol->value = position;
    } else if (!symbol->defined || symbol->section != assembler->current_section + 1 || symbol->value != position) {
        as_fail("inconsistent assembly passes");
    }
    return index;
}

static void normalize_symbol(Assembler *assembler, const char *name, char *output, size_t capacity) {
    if (name[0] == '.') {
        if (!assembler->local_scope) as_fail("local symbol has no parent label");
        size_t a = strlen(assembler->local_scope);
        size_t b = strlen(name);
        if (a + b + 1 > capacity) as_fail("symbol name is too long");
        memcpy(output, assembler->local_scope, a);
        memcpy(output + a, name, b + 1);
    } else {
        size_t length = strlen(name);
        if (length + 1 > capacity) as_fail("symbol name is too long");
        memcpy(output, name, length + 1);
    }
}

void as_add_relocation(Assembler *assembler, const char *name, uint32_t type, int64_t addend) {
    char normalized[256];
    normalize_symbol(assembler, name, normalized, sizeof(normalized));
    int symbol = as_symbol_find(assembler, normalized, 1);
    if (assembler->pass == 1) return;
    if (assembler->relocation_count == assembler->relocation_capacity) {
        size_t capacity = assembler->relocation_capacity ? assembler->relocation_capacity * 2 : 32;
        assembler->relocations = as_grow(assembler->relocations, assembler->relocation_capacity * sizeof(AsRelocation), capacity * sizeof(AsRelocation));
        assembler->relocation_capacity = capacity;
    }
    AsRelocation *relocation = &assembler->relocations[assembler->relocation_count++];
    relocation->section = assembler->current_section;
    relocation->offset = section_position(assembler, assembler->current_section);
    relocation->symbol = (uint32_t)symbol;
    relocation->type = type;
    relocation->addend = addend;
}

int as_read_source(const char *path, char **source) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    off_t end = lseek(descriptor, 0, SEEK_END);
    if (end < 0 || end > 4 * 1024 * 1024 || lseek(descriptor, 0, SEEK_SET) < 0) {
        close(descriptor);
        return -1;
    }
    char *buffer = malloc((size_t)end + 1);
    if (!buffer) {
        close(descriptor);
        return -1;
    }
    size_t read_count = 0;
    while (read_count < (size_t)end) {
        ssize_t count = read(descriptor, buffer + read_count, (size_t)end - read_count);
        if (count <= 0) {
            free(buffer);
            close(descriptor);
            return -1;
        }
        read_count += (size_t)count;
    }
    close(descriptor);
    buffer[read_count] = 0;
    *source = buffer;
    return 0;
}
