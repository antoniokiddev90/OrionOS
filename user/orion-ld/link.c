#include "ld.h"
#include <stdlib.h>
#include <string.h>

#define LD_SHT_NOBITS 8
#define LD_STB_LOCAL 0
#define LD_STB_GLOBAL 1
#define LD_STB_WEAK 2
#define LD_SHN_UNDEF 0
#define LD_SHN_ABS 0xfff1

static uint64_t align_address(uint64_t value, uint64_t alignment) {
    if (!alignment) alignment = 1;
    if (alignment & (alignment - 1)) ld_fail("section alignment is not a power of two");
    if (value > (uint64_t)-1 - (alignment - 1)) ld_fail("address overflow");
    return (value + alignment - 1) & ~(alignment - 1);
}

static int symbol_is_defined(const LdSymbol *symbol) {
    return symbol->section != LD_SHN_UNDEF;
}

static uint64_t input_symbol_address(LdInput *input, uint32_t index) {
    if (index >= input->symbol_count) ld_fail("invalid symbol index");
    LdSymbol *symbol = &input->symbols[index];
    if (symbol->section == LD_SHN_ABS) return symbol->value;
    if (symbol->section == LD_SHN_UNDEF) ld_fail("undefined symbol");
    if (symbol->section >= input->header->section_count || !input->kinds[symbol->section]) ld_fail("symbol refers to an unsupported section");
    LdSectionHeader *section = &input->sections[symbol->section];
    if (symbol->value > section->size) ld_fail("symbol value is outside its section");
    if (input->addresses[symbol->section] > (uint64_t)-1 - symbol->value) ld_fail("address overflow");
    return input->addresses[symbol->section] + symbol->value;
}

static int definition_find(Linker *linker, const char *name) {
    for (size_t i = 0; i < linker->definition_count; ++i) if (strcmp(linker->definitions[i].name, name) == 0) return (int)i;
    return -1;
}

static void add_definition(Linker *linker, LdInput *input, uint32_t index) {
    LdSymbol *symbol = &input->symbols[index];
    unsigned binding = symbol->info >> 4;
    if ((binding != LD_STB_GLOBAL && binding != LD_STB_WEAK) || !symbol_is_defined(symbol)) return;
    const char *name = ld_string_at(input->strings, input->strings_size, symbol->name);
    if (!*name) return;
    int found = definition_find(linker, name);
    if (found >= 0) {
        LdSymbol *previous = &linker->definitions[found].input->symbols[linker->definitions[found].symbol_index];
        unsigned previous_binding = previous->info >> 4;
        if (previous_binding == LD_STB_GLOBAL && binding == LD_STB_GLOBAL) ld_fail("duplicate global symbol");
        if (previous_binding == LD_STB_WEAK && binding == LD_STB_GLOBAL) {
            linker->definitions[found].input = input;
            linker->definitions[found].symbol_index = index;
            linker->definitions[found].value = input_symbol_address(input, index);
        }
        return;
    }
    linker->definitions = ld_grow(linker->definitions, linker->definition_count * sizeof(LdDefinition), (linker->definition_count + 1) * sizeof(LdDefinition));
    LdDefinition *definition = &linker->definitions[linker->definition_count++];
    definition->name = ld_duplicate(name);
    definition->value = input_symbol_address(input, index);
    definition->input = input;
    definition->symbol_index = index;
}

uint64_t ld_symbol_value(Linker *linker, LdInput *input, uint32_t symbol_index) {
    if (symbol_index >= input->symbol_count) ld_fail("invalid relocation symbol index");
    LdSymbol *symbol = &input->symbols[symbol_index];
    if (symbol->section == LD_SHN_ABS) return symbol->value;
    if (symbol->section != LD_SHN_UNDEF) return input_symbol_address(input, symbol_index);
    const char *name = ld_string_at(input->strings, input->strings_size, symbol->name);
    int definition = definition_find(linker, name);
    if (definition < 0) ld_fail("unresolved symbol");
    return linker->definitions[definition].value;
}

void ld_resolve_and_layout(Linker *linker) {
    if (linker->base_address > (uint64_t)-1 - 0x1000) ld_fail("address overflow");
    uint64_t cursor = linker->base_address + 0x1000;
    for (uint32_t kind = 1; kind <= 4; ++kind) {
        for (size_t i = 0; i < linker->input_count; ++i) {
            LdInput *input = &linker->inputs[i];
            for (uint16_t j = 1; j < input->header->section_count; ++j) {
                if (input->kinds[j] != kind) continue;
                LdSectionHeader *section = &input->sections[j];
                uint64_t alignment = section->alignment ? section->alignment : 1;
                if (alignment > 0x100000) ld_fail("section alignment is too large");
                cursor = align_address(cursor, alignment);
                input->addresses[j] = cursor;
                if (section->size > (uint64_t)-1 - cursor) ld_fail("address overflow");
                cursor += section->size;
                if (section->type != LD_SHT_NOBITS) linker->file_end = cursor;
            }
        }
    }
    linker->image_end = cursor;
    if (!linker->file_end || linker->file_end < linker->base_address) ld_fail("no loadable data");
    for (size_t i = 0; i < linker->input_count; ++i) {
        LdInput *input = &linker->inputs[i];
        for (uint32_t j = 0; j < input->symbol_count; ++j) add_definition(linker, input, j);
    }
    int found_entry = 0;
    for (size_t i = 0; i < linker->input_count && !found_entry; ++i) {
        LdInput *input = &linker->inputs[i];
        for (uint32_t j = 0; j < input->symbol_count; ++j) {
            LdSymbol *symbol = &input->symbols[j];
            if (!symbol_is_defined(symbol)) continue;
            const char *name = ld_string_at(input->strings, input->strings_size, symbol->name);
            if (strcmp(name, linker->entry_name) != 0) continue;
            linker->entry_address = input_symbol_address(input, j);
            found_entry = 1;
            break;
        }
    }
    if (!found_entry || linker->entry_address < linker->base_address || linker->entry_address >= linker->image_end) ld_fail("entry symbol not found");
}
