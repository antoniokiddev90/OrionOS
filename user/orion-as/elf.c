#include "as.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ELF_SHT_NULL 0
#define ELF_SHT_PROGBITS 1
#define ELF_SHT_SYMTAB 2
#define ELF_SHT_STRTAB 3
#define ELF_SHT_RELA 4
#define ELF_SHT_NOBITS 8

typedef struct __attribute__((packed)) {
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
} ElfHeader;

typedef struct __attribute__((packed)) {
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
} ElfSectionHeader;

typedef struct __attribute__((packed)) {
    uint32_t name;
    unsigned char info;
    unsigned char other;
    uint16_t section;
    uint64_t value;
    uint64_t size;
} ElfSymbol;

typedef struct __attribute__((packed)) {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} ElfRela;

static void buffer_append(AsBuffer *buffer, const void *source, size_t size) {
    as_buffer_reserve(buffer, size);
    memcpy(buffer->data + buffer->size, source, size);
    buffer->size += size;
}

static size_t append_string(AsBuffer *buffer, const char *text) {
    size_t offset = buffer->size;
    buffer_append(buffer, text, strlen(text) + 1);
    return offset;
}

static size_t align_offset(size_t value, size_t alignment) {
    size_t remainder = value % alignment;
    return remainder ? value + alignment - remainder : value;
}

static void ensure_file_size(AsBuffer *file, size_t size) {
    if (size > file->size) {
        as_buffer_reserve(file, size - file->size);
        memset(file->data + file->size, 0, size - file->size);
        file->size = size;
    }
}

static size_t section_relocation_count(const Assembler *assembler, size_t section) {
    size_t count = 0;
    for (size_t i = 0; i < assembler->relocation_count; ++i) if (assembler->relocations[i].section == section) ++count;
    return count;
}

static void write_output(const char *path, const unsigned char *data, size_t size) {
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (descriptor < 0) as_fail("cannot create output");
    size_t written = 0;
    while (written < size) {
        size_t request = size - written;
        if (request > 4096) request = 4096;
        ssize_t count = write(descriptor, data + written, request);
        if (count <= 0) {
            close(descriptor);
            as_fail("cannot write output");
        }
        written += (size_t)count;
    }
    close(descriptor);
}

void as_write_object(Assembler *assembler, const char *path) {
    size_t section_count = assembler->section_count;
    size_t relocation_section_count = 0;
    for (size_t i = 0; i < section_count; ++i) if (section_relocation_count(assembler, i)) ++relocation_section_count;
    size_t symtab_index = 1 + section_count + relocation_section_count;
    size_t strtab_index = symtab_index + 1;
    size_t shstrtab_index = strtab_index + 1;
    size_t total_sections = shstrtab_index + 1;
    uint32_t *section_name_offsets = malloc(total_sections * sizeof(uint32_t));
    size_t *section_offsets = malloc(total_sections * sizeof(size_t));
    uint32_t *relocation_indices = malloc(section_count * sizeof(uint32_t));
    size_t *symbol_order = malloc((assembler->symbol_count ? assembler->symbol_count : 1) * sizeof(size_t));
    uint32_t *symbol_map = malloc((assembler->symbol_count ? assembler->symbol_count : 1) * sizeof(uint32_t));
    size_t *symbol_name_offsets = malloc((assembler->symbol_count ? assembler->symbol_count : 1) * sizeof(size_t));
    if (!section_name_offsets || !section_offsets || !relocation_indices || !symbol_order || !symbol_map || !symbol_name_offsets) as_fail("out of memory");
    for (size_t i = 0; i < section_count; ++i) relocation_indices[i] = 0;
    size_t next_relocation_index = 1 + section_count;
    for (size_t i = 0; i < section_count; ++i) if (section_relocation_count(assembler, i)) relocation_indices[i] = (uint32_t)next_relocation_index++;
    size_t local_count = 0;
    size_t symbol_order_count = 0;
    for (size_t i = 0; i < assembler->symbol_count; ++i) if (!assembler->symbols[i].binding && assembler->symbols[i].defined) symbol_order[symbol_order_count++] = i;
    local_count = symbol_order_count;
    for (size_t i = 0; i < assembler->symbol_count; ++i) if (assembler->symbols[i].binding || !assembler->symbols[i].defined) symbol_order[symbol_order_count++] = i;
    for (size_t i = 0; i < symbol_order_count; ++i) symbol_map[symbol_order[i]] = (uint32_t)i + 1;
    AsBuffer string_table = {0};
    unsigned char zero = 0;
    buffer_append(&string_table, &zero, 1);
    for (size_t i = 0; i < symbol_order_count; ++i) symbol_name_offsets[symbol_order[i]] = append_string(&string_table, assembler->symbols[symbol_order[i]].name);
    AsBuffer section_names = {0};
    buffer_append(&section_names, &zero, 1);
    section_name_offsets[0] = 0;
    for (size_t i = 0; i < section_count; ++i) section_name_offsets[i + 1] = (uint32_t)append_string(&section_names, assembler->sections[i].name);
    size_t next_name_index = 1 + section_count;
    for (size_t i = 0; i < section_count; ++i) {
        if (!relocation_indices[i]) continue;
        char name[256];
        size_t length = strlen(assembler->sections[i].name);
        if (length + 6 >= sizeof(name)) as_fail("section name is too long");
        memcpy(name, ".rela", 5);
        memcpy(name + 5, assembler->sections[i].name, length + 1);
        section_name_offsets[next_name_index++] = (uint32_t)append_string(&section_names, name);
    }
    section_name_offsets[symtab_index] = (uint32_t)append_string(&section_names, ".symtab");
    section_name_offsets[strtab_index] = (uint32_t)append_string(&section_names, ".strtab");
    section_name_offsets[shstrtab_index] = (uint32_t)append_string(&section_names, ".shstrtab");
    AsBuffer symbol_table = {0};
    ElfSymbol null_symbol;
    memset(&null_symbol, 0, sizeof(null_symbol));
    buffer_append(&symbol_table, &null_symbol, sizeof(null_symbol));
    for (size_t i = 0; i < symbol_order_count; ++i) {
        AsSymbol *source = &assembler->symbols[symbol_order[i]];
        ElfSymbol symbol;
        symbol.name = (uint32_t)symbol_name_offsets[symbol_order[i]];
        symbol.info = (unsigned char)((source->binding ? 1 : 0) << 4);
        symbol.other = 0;
        symbol.section = source->defined ? source->section : 0;
        symbol.value = source->defined ? source->value : 0;
        symbol.size = 0;
        buffer_append(&symbol_table, &symbol, sizeof(symbol));
    }
    size_t file_offset = sizeof(ElfHeader);
    file_offset = align_offset(file_offset, 8);
    for (size_t i = 0; i < total_sections; ++i) section_offsets[i] = 0;
    for (size_t i = 0; i < section_count; ++i) {
        AsSection *section = &assembler->sections[i];
        file_offset = align_offset(file_offset, (size_t)(section->alignment ? section->alignment : 1));
        section_offsets[i + 1] = file_offset;
        if (section->type != ELF_SHT_NOBITS) file_offset += section->bytes.size;
    }
    next_relocation_index = 1 + section_count;
    AsBuffer *relocation_buffers = malloc(section_count * sizeof(AsBuffer));
    if (!relocation_buffers && section_count) as_fail("out of memory");
    if (section_count) memset(relocation_buffers, 0, section_count * sizeof(AsBuffer));
    for (size_t i = 0; i < assembler->relocation_count; ++i) {
        AsRelocation *source = &assembler->relocations[i];
        if (source->symbol >= assembler->symbol_count) as_fail("invalid relocation symbol");
        ElfRela relocation;
        relocation.offset = source->offset;
        relocation.info = ((uint64_t)symbol_map[source->symbol] << 32) | source->type;
        relocation.addend = source->addend;
        buffer_append(&relocation_buffers[source->section], &relocation, sizeof(relocation));
    }
    for (size_t i = 0; i < section_count; ++i) {
        if (!relocation_indices[i]) continue;
        file_offset = align_offset(file_offset, 8);
        section_offsets[relocation_indices[i]] = file_offset;
        file_offset += relocation_buffers[i].size;
    }
    file_offset = align_offset(file_offset, 8);
    section_offsets[symtab_index] = file_offset;
    file_offset += symbol_table.size;
    section_offsets[strtab_index] = file_offset;
    file_offset += string_table.size;
    section_offsets[shstrtab_index] = file_offset;
    file_offset += section_names.size;
    size_t section_header_offset = align_offset(file_offset, 8);
    size_t total_file_size = section_header_offset + total_sections * sizeof(ElfSectionHeader);
    AsBuffer output = {0};
    ensure_file_size(&output, total_file_size);
    for (size_t i = 0; i < section_count; ++i) {
        AsSection *section = &assembler->sections[i];
        if (section->type != ELF_SHT_NOBITS && section->bytes.size) memcpy(output.data + section_offsets[i + 1], section->bytes.data, section->bytes.size);
    }
    next_relocation_index = 1 + section_count;
    for (size_t i = 0; i < section_count; ++i) {
        if (!relocation_indices[i]) continue;
        memcpy(output.data + section_offsets[next_relocation_index++], relocation_buffers[i].data, relocation_buffers[i].size);
    }
    memcpy(output.data + section_offsets[symtab_index], symbol_table.data, symbol_table.size);
    memcpy(output.data + section_offsets[strtab_index], string_table.data, string_table.size);
    memcpy(output.data + section_offsets[shstrtab_index], section_names.data, section_names.size);
    ElfSectionHeader *headers = (ElfSectionHeader *)(output.data + section_header_offset);
    memset(headers, 0, total_sections * sizeof(ElfSectionHeader));
    for (size_t i = 0; i < section_count; ++i) {
        AsSection *source = &assembler->sections[i];
        ElfSectionHeader *header = &headers[i + 1];
        header->name = section_name_offsets[i + 1];
        header->type = source->type;
        header->flags = source->flags;
        header->offset = section_offsets[i + 1];
        header->size = source->type == ELF_SHT_NOBITS ? source->bss_size : source->bytes.size;
        header->alignment = source->alignment ? source->alignment : 1;
    }
    next_relocation_index = 1 + section_count;
    for (size_t i = 0; i < section_count; ++i) {
        if (!relocation_indices[i]) continue;
        ElfSectionHeader *header = &headers[next_relocation_index];
        header->name = section_name_offsets[next_relocation_index];
        header->type = ELF_SHT_RELA;
        header->offset = section_offsets[next_relocation_index];
        header->size = relocation_buffers[i].size;
        header->link = (uint32_t)symtab_index;
        header->info = (uint32_t)(i + 1);
        header->alignment = 8;
        header->entry_size = sizeof(ElfRela);
        ++next_relocation_index;
    }
    headers[symtab_index].name = section_name_offsets[symtab_index];
    headers[symtab_index].type = ELF_SHT_SYMTAB;
    headers[symtab_index].offset = section_offsets[symtab_index];
    headers[symtab_index].size = symbol_table.size;
    headers[symtab_index].link = (uint32_t)strtab_index;
    headers[symtab_index].info = (uint32_t)(local_count + 1);
    headers[symtab_index].alignment = 8;
    headers[symtab_index].entry_size = sizeof(ElfSymbol);
    headers[strtab_index].name = section_name_offsets[strtab_index];
    headers[strtab_index].type = ELF_SHT_STRTAB;
    headers[strtab_index].offset = section_offsets[strtab_index];
    headers[strtab_index].size = string_table.size;
    headers[strtab_index].alignment = 1;
    headers[shstrtab_index].name = section_name_offsets[shstrtab_index];
    headers[shstrtab_index].type = ELF_SHT_STRTAB;
    headers[shstrtab_index].offset = section_offsets[shstrtab_index];
    headers[shstrtab_index].size = section_names.size;
    headers[shstrtab_index].alignment = 1;
    ElfHeader header;
    memset(&header, 0, sizeof(header));
    header.ident[0] = 0x7f;
    header.ident[1] = 'E';
    header.ident[2] = 'L';
    header.ident[3] = 'F';
    header.ident[4] = 2;
    header.ident[5] = 1;
    header.ident[6] = 1;
    header.type = 1;
    header.machine = 62;
    header.version = 1;
    header.section_offset = section_header_offset;
    header.header_size = sizeof(ElfHeader);
    header.section_entry_size = sizeof(ElfSectionHeader);
    header.section_count = (uint16_t)total_sections;
    header.section_string_index = (uint16_t)shstrtab_index;
    memcpy(output.data, &header, sizeof(header));
    write_output(path, output.data, output.size);
    free(section_name_offsets);
    free(section_offsets);
    free(relocation_indices);
    free(symbol_order);
    free(symbol_map);
    free(symbol_name_offsets);
    for (size_t i = 0; i < section_count; ++i) free(relocation_buffers[i].data);
    free(relocation_buffers);
    free(string_table.data);
    free(section_names.data);
    free(symbol_table.data);
    free(output.data);
}
