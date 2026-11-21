#include "ld.h"
#include <stdlib.h>

#define LD_SHT_RELA 4
#define LD_SHT_NOBITS 8
#define LD_R_X86_64_64 1
#define LD_R_X86_64_PC32 2
#define LD_R_X86_64_PLT32 4
#define LD_R_X86_64_32 10
#define LD_R_X86_64_32S 11
#define LD_R_X86_64_PC8 15

static void write_little(unsigned char *bytes, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
}

static int relocation_width(uint32_t type) {
    if (type == LD_R_X86_64_64) return 8;
    if (type == LD_R_X86_64_PC8) return 1;
    if (type == LD_R_X86_64_PC32 || type == LD_R_X86_64_PLT32 || type == LD_R_X86_64_32 || type == LD_R_X86_64_32S) return 4;
    return 0;
}

void ld_apply_relocations(Linker *linker) {
    for (size_t i = 0; i < linker->input_count; ++i) {
        LdInput *input = &linker->inputs[i];
        for (uint16_t section_index = 0; section_index < input->header->section_count; ++section_index) {
            LdSectionHeader *section = &input->sections[section_index];
            if (section->type != LD_SHT_RELA) continue;
            if (section->info >= input->header->section_count || section->link != input->symbol_table_index || section->entry_size != sizeof(LdRela) || section->size % sizeof(LdRela) || !ld_range_ok(input->size, section->offset, section->size)) ld_fail("invalid relocation table");
            uint32_t target_index = section->info;
            if (!input->kinds[target_index] || input->sections[target_index].type == LD_SHT_NOBITS) ld_fail("unsupported relocation target");
            LdRela *relocations = (LdRela *)(input->data + section->offset);
            size_t count = (size_t)(section->size / sizeof(LdRela));
            for (size_t j = 0; j < count; ++j) {
                uint32_t type = (uint32_t)relocations[j].info;
                uint32_t symbol_index = (uint32_t)(relocations[j].info >> 32);
                int width = relocation_width(type);
                if (!width) ld_fail("unsupported relocation type");
                LdSectionHeader *target_section = &input->sections[target_index];
                if (relocations[j].offset > target_section->size || (uint64_t)width > target_section->size - relocations[j].offset) ld_fail("relocation is outside its section");
                uint64_t place = input->addresses[target_index] + relocations[j].offset;
                uint64_t symbol_value = ld_symbol_value(linker, input, symbol_index);
                int64_t addend = relocations[j].addend;
                int64_t result;
                if (type == LD_R_X86_64_PC32 || type == LD_R_X86_64_PLT32 || type == LD_R_X86_64_PC8) result = (int64_t)symbol_value + addend - (int64_t)place;
                else result = (int64_t)symbol_value + addend;
                uint64_t output_offset = place - linker->base_address;
                if (output_offset > linker->output.size || (uint64_t)width > linker->output.size - output_offset) ld_fail("relocation is outside the output image");
                unsigned char *destination = linker->output.data + output_offset;
                if (type == LD_R_X86_64_64) {
                    write_little(destination, (uint64_t)result, 8);
                } else if (type == LD_R_X86_64_32) {
                    if (result < 0 || (uint64_t)result > 0xffffffffULL) ld_fail("R_X86_64_32 overflow");
                    write_little(destination, (uint32_t)result, 4);
                } else if (type == LD_R_X86_64_32S) {
                    if (result < -2147483648LL || result > 2147483647LL) ld_fail("R_X86_64_32S overflow");
                    write_little(destination, (uint32_t)result, 4);
                } else if (type == LD_R_X86_64_PC8) {
                    if (result < -128 || result > 127) ld_fail("PC-relative relocation overflow");
                    write_little(destination, (uint8_t)result, 1);
                } else {
                    if (result < -2147483648LL || result > 2147483647LL) ld_fail("PC-relative relocation overflow");
                    write_little(destination, (uint32_t)result, 4);
                }
            }
        }
    }
}
