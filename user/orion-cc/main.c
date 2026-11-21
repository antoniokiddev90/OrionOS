#include "compiler.h"

int unlink(const char *path);

int main(int argc, char **argv) {
    compiler.program_path = argc > 0 ? argv[0] : "orion-cc";
    int assembly_only = 0;
    int compile_only = 0;
    int no_start = 0;
    char *output_path = NULL;
    char *source_path = NULL;
    size_t extra_capacity = argc > 0 ? (size_t)argc : 1;
    if (extra_capacity > (size_t)-1 / sizeof(char *)) fail("too many arguments");
    char **extra_objects = malloc(extra_capacity * sizeof(char *));
    size_t extra_count = 0;
    if (!extra_objects) fail("out of memory");
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "-S") == 0) { assembly_only = 1; compile_only = 0; continue; }
        if (strcmp(argv[i], "-c") == 0) { compile_only = 1; assembly_only = 0; continue; }
        if (strcmp(argv[i], "-nostart") == 0) { no_start = 1; continue; }
        if (strcmp(argv[i], "-nostdlib") == 0 || strcmp(argv[i], "-static") == 0) continue;
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-T") == 0) {
            if (++i >= argc) fail("option requires a value");
            if (strcmp(argv[i - 1], "-o") == 0) output_path = argv[i];
            else compiler.script_path = argv[i];
            continue;
        }
        if (strcmp(argv[i], "-I") == 0) {
            if (++i >= argc) fail("-I requires a directory");
            continue;
        }
        if (strncmp(argv[i], "-I", 2) == 0) continue;
        if (argv[i][0] == '-') fail("unsupported compiler option");
        size_t length = strlen(argv[i]);
        if (length >= 2 && strcmp(argv[i] + length - 2, ".c") == 0) {
            if (source_path) fail("only one C source file is supported");
            source_path = argv[i];
        } else extra_objects[extra_count++] = argv[i];
    }
    if (!source_path) {
        print_help();
        return 1;
    }
    compiler.source_name = source_path;
    size_t source_size = 0;
    char *source = read_file(source_path, &source_size);
    (void)source_size;
    char *expanded = preprocess(source);
    free(source);
    initialize_builtin_aliases();
    tokenize(expanded);
    free(expanded);
    parse_translation_unit();
    if (!compiler.saw_start && !no_start && !assembly_only && !compile_only) append_start_wrapper();
    if (assembly_only && !compiler.saw_start && !no_start) append_start_wrapper();
    Buffer assembly = {0};
    buffer_bytes(&assembly, compiler.text.data, compiler.text.size);
    if (compiler.rodata.size) {
        buffer_text(&assembly, "section .rodata\n");
        buffer_bytes(&assembly, compiler.rodata.data, compiler.rodata.size);
    }
    if (compiler.data.size) {
        buffer_text(&assembly, "section .data\n");
        buffer_bytes(&assembly, compiler.data.data, compiler.data.size);
    }
    if (compiler.bss.size) {
        buffer_text(&assembly, "section .bss\n");
        buffer_bytes(&assembly, compiler.bss.data, compiler.bss.size);
    }
    const char *suffix = assembly_only ? ".s" : compile_only ? ".o" : ".elf";
    if (!output_path) output_path = default_output_name(source_path, suffix);
    char *temporary_assembly = NULL;
    char *temporary_object = NULL;
    if (assembly_only) {
        write_file(output_path, assembly.data, assembly.size);
        return 0;
    }
    Buffer temporary = {0};
    buffer_text(&temporary, source_path);
    buffer_text(&temporary, ".orioncc.");
    buffer_number(&temporary, (unsigned long long)getpid());
    buffer_text(&temporary, ".s");
    buffer_char(&temporary, 0);
    temporary_assembly = temporary.data;
    temporary_object = path_with_suffix(temporary_assembly, ".o");
    write_file(temporary_assembly, assembly.data, assembly.size);
    if (compile_only) {
        assemble_source(temporary_assembly, output_path);
        unlink(temporary_assembly);
        free(temporary_assembly);
        free(temporary_object);
        return 0;
    }
    assemble_source(temporary_assembly, temporary_object);
    link_objects(output_path, temporary_object, extra_objects, extra_count);
    unlink(temporary_assembly);
    unlink(temporary_object);
    free(temporary_assembly);
    free(temporary_object);
    return 0;
}
