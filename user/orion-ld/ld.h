#ifndef ORION_LD_H
#define ORION_LD_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_size;
} LdSectionHeader;

typedef struct {
    uint32_t name;
    unsigned char info;
    unsigned char other;
    uint16_t section;
    uint64_t value;
    uint64_t size;
} LdSymbol;

typedef struct {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} LdRela;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} LdProgramHeader;

typedef struct {
    unsigned char ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_offset;
    uint64_t section_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_entry_size;
    uint16_t program_count;
    uint16_t section_entry_size;
    uint16_t section_count;
    uint16_t section_string_index;
} LdElfHeader;

typedef struct {
    const char *path;
    unsigned char *data;
    size_t size;
    LdElfHeader *header;
    LdSectionHeader *sections;
    const char *section_strings;
    size_t section_strings_size;
    LdSymbol *symbols;
    size_t symbol_count;
    const char *strings;
    size_t strings_size;
    uint32_t symbol_table_index;
    uint32_t *kinds;
    uint64_t *addresses;
} LdInput;

typedef struct {
    char *name;
    uint64_t value;
    LdInput *input;
    uint32_t symbol_index;
} LdDefinition;

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} LdBuffer;

typedef struct {
    char **input_paths;
    size_t input_count;
    const char *script_path;
    const char *output_path;
    const char *entry_name;
    uint64_t base_address;
    uint64_t entry_address;
    LdInput *inputs;
    LdDefinition *definitions;
    size_t definition_count;
    LdBuffer output;
    uint64_t image_end;
    uint64_t file_end;
} Linker;

void ld_fail(const char *message) __attribute__((noreturn));
void *ld_grow(void *memory, size_t old_size, size_t new_size);
char *ld_duplicate(const char *text);
void ld_buffer_reserve(LdBuffer *buffer, size_t needed);
void ld_buffer_put(LdBuffer *buffer, const void *data, size_t size);
void ld_buffer_zero(LdBuffer *buffer, size_t size);
int ld_range_ok(size_t file_size, uint64_t offset, uint64_t length);
const char *ld_string_at(const char *table, size_t size, uint32_t offset);
unsigned char *ld_read_file(const char *path, size_t *size);
void ld_parse_arguments(Linker *linker, int argc, char **argv);
void ld_read_script(Linker *linker);
void ld_load_inputs(Linker *linker);
void ld_resolve_and_layout(Linker *linker);
uint64_t ld_symbol_value(Linker *linker, LdInput *input, uint32_t symbol_index);
void ld_apply_relocations(Linker *linker);
void ld_write_executable(Linker *linker);

#endif
