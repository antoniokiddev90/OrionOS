#include "ld.h"
#include <stdlib.h>
#include <string.h>

#define LD_ET_REL 1
#define LD_EM_X86_64 62
#define LD_EV_CURRENT 1
#define LD_SHT_PROGBITS 1
#define LD_SHT_SYMTAB 2
#define LD_SHT_STRTAB 3
#define LD_SHT_RELA 4
#define LD_SHT_NOBITS 8
#define LD_SHF_ALLOC 2

static int name_is(const char *name, const char *expected) {
    return strcmp(name, expected) == 0;
}

static int name_starts(const char *name, const char *prefix) {
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

static uint32_t classify_section(const char *name) {
    if (name_is(name, ".text") || name_starts(name, ".text.") || name_starts(name, ".gnu.linkonce.t.") || name_is(name, ".multiboot")) return 1;
    if (name_is(name, ".rodata") || name_starts(name, ".rodata.") || name_is(name, ".eh_frame") || name_is(name, ".eh_frame_hdr") || name_is(name, ".gcc_except_table") || name_starts(name, ".gnu.linkonce.r.")) return 2;
    if (name_is(name, ".data") || name_starts(name, ".data.") || name_is(name, ".sdata") || name_starts(name, ".sdata.") || name_is(name, ".got") || name_starts(name, ".got.") || name_is(name, ".preinit_array") || name_starts(name, ".preinit_array.") || name_is(name, ".init_array") || name_starts(name, ".init_array.") || name_is(name, ".fini_array") || name_starts(name, ".fini_array.") || name_is(name, ".ctors") || name_is(name, ".dtors") || name_starts(name, ".gnu.linkonce.d.")) return 3;
    if (name_is(name, ".bss") || name_starts(name, ".bss.") || name_is(name, ".sbss") || name_starts(name, ".sbss.") || name_starts(name, ".gnu.linkonce.b.")) return 4;
    return 0;
}

static void load_one(LdInput *input, const char *path) {
    input->path = path;
    input->data = ld_read_file(path, &input->size);
    if (input->size < sizeof(LdElfHeader)) ld_fail("invalid ELF input");
    input->header = (LdElfHeader *)input->data;
    LdElfHeader *header = input->header;
    if (header->ident[0] != 0x7f || header->ident[1] != 'E' || header->ident[2] != 'L' || header->ident[3] != 'F' || header->ident[4] != 2 || header->ident[5] != 1 || header->ident[6] != 1 || header->type != LD_ET_REL || header->machine != LD_EM_X86_64 || header->version != LD_EV_CURRENT || header->header_size != sizeof(LdElfHeader) || header->section_entry_size != sizeof(LdSectionHeader) || !header->section_count) ld_fail("unsupported ELF input");
    if (!ld_range_ok(input->size, header->section_offset, (uint64_t)header->section_count * sizeof(LdSectionHeader))) ld_fail("invalid section table");
    input->sections = (LdSectionHeader *)(input->data + header->section_offset);
    if (header->section_string_index >= header->section_count) ld_fail("invalid section string table");
    LdSectionHeader *section_names = &input->sections[header->section_string_index];
    if (section_names->type != LD_SHT_STRTAB || !ld_range_ok(input->size, section_names->offset, section_names->size)) ld_fail("invalid section string table");
    input->section_strings = (const char *)(input->data + section_names->offset);
    input->section_strings_size = (size_t)section_names->size;
    if (!input->section_strings_size || input->section_strings[0]) ld_fail("invalid section string table");
    input->kinds = malloc((size_t)header->section_count * sizeof(uint32_t));
    input->addresses = malloc((size_t)header->section_count * sizeof(uint64_t));
    if (!input->kinds || !input->addresses) ld_fail("out of memory");
    memset(input->kinds, 0, (size_t)header->section_count * sizeof(uint32_t));
    memset(input->addresses, 0, (size_t)header->section_count * sizeof(uint64_t));
    for (uint16_t i = 0; i < header->section_count; ++i) {
        LdSectionHeader *section = &input->sections[i];
        const char *name = ld_string_at(input->section_strings, input->section_strings_size, section->name);
        if (section->type != LD_SHT_NOBITS && !ld_range_ok(input->size, section->offset, section->size)) ld_fail("section is outside the input file");
        uint32_t kind = classify_section(name);
        if ((section->flags & LD_SHF_ALLOC) && (section->type == LD_SHT_PROGBITS || section->type == LD_SHT_NOBITS) && !kind) ld_fail("unsupported allocatable section");
        if (kind && section->type != LD_SHT_PROGBITS && section->type != LD_SHT_NOBITS) ld_fail("unsupported input section type");
        input->kinds[i] = kind;
    }
    int found_symbols = 0;
    for (uint16_t i = 0; i < header->section_count; ++i) {
        LdSectionHeader *section = &input->sections[i];
        if (section->type != LD_SHT_SYMTAB) continue;
        if (found_symbols || section->entry_size != sizeof(LdSymbol) || section->size % sizeof(LdSymbol) || !ld_range_ok(input->size, section->offset, section->size) || section->link >= header->section_count || input->sections[section->link].type != LD_SHT_STRTAB) ld_fail("invalid symbol table");
        found_symbols = 1;
        input->symbol_table_index = i;
        input->symbols = (LdSymbol *)(input->data + section->offset);
        input->symbol_count = (size_t)(section->size / sizeof(LdSymbol));
        LdSectionHeader *strings = &input->sections[section->link];
        if (!ld_range_ok(input->size, strings->offset, strings->size)) ld_fail("invalid symbol string table");
        input->strings = (const char *)(input->data + strings->offset);
        input->strings_size = (size_t)strings->size;
        if (!input->strings_size || input->strings[0]) ld_fail("invalid symbol string table");
    }
    if (!found_symbols || !input->symbol_count) ld_fail("missing symbol table");
    for (size_t i = 0; i < input->symbol_count; ++i) {
        LdSymbol *symbol = &input->symbols[i];
        if (symbol->name) (void)ld_string_at(input->strings, input->strings_size, symbol->name);
        if (symbol->section != 0 && symbol->section != 0xfff1 && symbol->section >= header->section_count) ld_fail("invalid symbol section index");
    }
}

void ld_load_inputs(Linker *linker) {
    linker->inputs = malloc(linker->input_count * sizeof(LdInput));
    if (!linker->inputs) ld_fail("out of memory");
    memset(linker->inputs, 0, linker->input_count * sizeof(LdInput));
    for (size_t i = 0; i < linker->input_count; ++i) load_one(&linker->inputs[i], linker->input_paths[i]);
}
