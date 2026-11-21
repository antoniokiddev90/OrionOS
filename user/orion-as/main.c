#include "as.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *input_path = NULL;
    const char *output_path = "a.o";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            puts("orion-as - native x86 assembler\nUsage: orion-as [-f elf64] [-o output] input");
            return 0;
        }
        if (strcmp(argv[i], "-f") == 0) {
            if (++i >= argc || strcmp(argv[i], "elf64") != 0) as_fail("only elf64 is supported");
            continue;
        }
        if (strcmp(argv[i], "-o") == 0) {
            if (++i >= argc) as_fail("-o requires an output path");
            output_path = argv[i];
            continue;
        }
        if (argv[i][0] == '-') as_fail("unsupported option");
        if (input_path) as_fail("only one input file is supported");
        input_path = argv[i];
    }
    if (!input_path) as_fail("no input file");
    char *source;
    if (as_read_source(input_path, &source) != 0) as_fail("cannot read input");
    Assembler assembler;
    memset(&assembler, 0, sizeof(assembler));
    assembler.source_name = input_path;
    assembler.mode_bits = 64;
    if (as_section_find(&assembler, ".text", 1) < 0 || as_section_find(&assembler, ".rodata", 1) < 0 || as_section_find(&assembler, ".data", 1) < 0 || as_section_find(&assembler, ".bss", 1) < 0) as_fail("cannot initialize sections");
    as_parse_source(&assembler, source);
    as_write_object(&assembler, output_path);
    free(source);
    return 0;
}
