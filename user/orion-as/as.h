#ifndef ORION_AS_H
#define ORION_AS_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} AsBuffer;

typedef struct {
    char *name;
    uint64_t flags;
    uint32_t type;
    uint64_t alignment;
    AsBuffer bytes;
    uint64_t bss_size;
} AsSection;

typedef struct {
    char *name;
    uint64_t value;
    uint16_t section;
    unsigned char binding;
    int defined;
} AsSymbol;

typedef struct {
    uint32_t section;
    uint64_t offset;
    uint32_t symbol;
    uint32_t type;
    int64_t addend;
} AsRelocation;

typedef struct {
    AsSection *sections;
    size_t section_count;
    size_t section_capacity;
    AsSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    AsRelocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;
    uint32_t current_section;
    uint32_t mode_bits;
    int pass;
    char *local_scope;
    const char *source_name;
} Assembler;

void as_fail(const char *message) __attribute__((noreturn));
void *as_grow(void *memory, size_t old_size, size_t new_size);
char *as_duplicate(const char *text);
void as_buffer_reserve(AsBuffer *buffer, size_t additional);
void as_emit_byte(Assembler *assembler, unsigned char value);
void as_emit_integer(Assembler *assembler, uint64_t value, size_t width);
void as_emit_zeros(Assembler *assembler, size_t count);
void as_align(Assembler *assembler, uint64_t alignment);
int as_section_find(Assembler *assembler, const char *name, int create);
int as_symbol_find(Assembler *assembler, const char *name, int create);
int as_symbol_define(Assembler *assembler, const char *name);
void as_add_relocation(Assembler *assembler, const char *name, uint32_t type, int64_t addend);
int as_expression(Assembler *assembler, const char *text, int64_t *value, int *symbol, int64_t *addend);
void as_parse_source(Assembler *assembler, char *source);
void as_encode_instruction(Assembler *assembler, char *line);
void as_write_object(Assembler *assembler, const char *path);
int as_read_source(const char *path, char **source);

#endif
