#include "compiler.h"
#include <sys/wait.h>

int unlink(const char *path);

char *read_file(const char *path, size_t *size) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) fail("cannot open input file");
    off_t end = lseek(descriptor, 0, SEEK_END);
    if (end < 0 || end > 4 * 1024 * 1024 || lseek(descriptor, 0, SEEK_SET) < 0) {
        close(descriptor);
        fail("cannot read input file");
    }
    char *data = malloc((size_t)end + 1);
    if (!data) {
        close(descriptor);
        fail("out of memory");
    }
    size_t used = 0;
    while (used < (size_t)end) {
        ssize_t count = read(descriptor, data + used, (size_t)end - used);
        if (count <= 0) {
            close(descriptor);
            free(data);
            fail("cannot read input file");
        }
        used += (size_t)count;
    }
    close(descriptor);
    data[used] = 0;
    *size = used;
    return data;
}

void write_file(const char *path, const char *data, size_t size) {
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (descriptor < 0) fail("cannot create output file");
    size_t written = 0;
    while (written < size) {
        ssize_t count = write(descriptor, data + written, size - written);
        if (count <= 0) {
            close(descriptor);
            fail("cannot write output file");
        }
        written += (size_t)count;
    }
    if (close(descriptor) != 0) fail("cannot close output file");
}

char *path_with_suffix(const char *path, const char *suffix) {
    Buffer result = {0};
    buffer_text(&result, path);
    buffer_text(&result, suffix);
    buffer_char(&result, 0);
    return result.data;
}

char *default_output_name(const char *source, const char *suffix) {
    const char *base = source;
    for (const char *cursor = source; *cursor; ++cursor) if (*cursor == '/') base = cursor + 1;
    size_t length = strlen(base);
    const char *dot = NULL;
    for (size_t i = 0; i < length; ++i) if (base[i] == '.') dot = base + i;
    Buffer result = {0};
    buffer_bytes(&result, base, dot ? (size_t)(dot - base) : length);
    buffer_text(&result, suffix);
    buffer_char(&result, 0);
    return result.data;
}

char *tool_path(const char *tool) {
    const char *slash = NULL;
    for (const char *cursor = compiler.program_path; *cursor; ++cursor) if (*cursor == '/') slash = cursor;
    if (slash) {
        Buffer path = {0};
        buffer_bytes(&path, compiler.program_path, (size_t)(slash - compiler.program_path + 1));
        buffer_text(&path, tool);
        buffer_char(&path, 0);
        return path.data;
    }
#if defined(__linux__)
    const char *search_path = getenv("PATH");
    if (search_path) {
        const char *entry = search_path;
        for (;;) {
            const char *end = entry;
            while (*end && *end != ':') ++end;
            Buffer path = {0};
            if (end == entry) buffer_char(&path, '.');
            else buffer_bytes(&path, entry, (size_t)(end - entry));
            if (!path.size || path.data[path.size - 1] != '/') buffer_char(&path, '/');
            buffer_text(&path, tool);
            buffer_char(&path, 0);
            if (access(path.data, X_OK) == 0) return path.data;
            free(path.data);
            if (!*end) break;
            entry = end + 1;
        }
    }
    fail("tool not found in PATH");
#else
    Buffer path = {0};
    buffer_text(&path, "/bin/");
    buffer_text(&path, tool);
    buffer_char(&path, 0);
    return path.data;
#endif
    return NULL;
}

int run_tool(char *const arguments[]) {
    pid_t child = fork();
    if (child < 0) fail("cannot start tool");
    if (child == 0) {
        execve(arguments[0], arguments, NULL);
        exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) fail("cannot wait for tool");
    return status == 0 ? 0 : -1;
}

void append_start_wrapper(void) {
    buffer_text(&compiler.text, "section .text\nglobal _start\n_start:\nand rsp, -16\ncall main\nmov rdi, rax\nmov rax, 17\nint 0x9e\n__orion_cc_halt:\nhlt\njmp __orion_cc_halt\n");
    compiler.saw_start = 1;
}

void assemble_source(const char *source_path, const char *object_path) {
    compiler.assembler_path = tool_path("orion-as");
    char *arguments[7];
    arguments[0] = (char *)compiler.assembler_path;
    arguments[1] = "-f";
    arguments[2] = "elf64";
    arguments[3] = "-o";
    arguments[4] = (char *)object_path;
    arguments[5] = (char *)source_path;
    arguments[6] = NULL;
    if (run_tool(arguments) != 0) fail("assembler failed");
}

void link_objects(const char *output_path, const char *object_path, char **extra_objects, size_t extra_count) {
    compiler.linker_path = tool_path("orion-ld");
    size_t count = 7 + extra_count;
    char **arguments = malloc(count * sizeof(char *));
    if (!arguments) fail("out of memory");
    size_t position = 0;
    arguments[position++] = (char *)compiler.linker_path;
    arguments[position++] = "-T";
    arguments[position++] = (char *)(compiler.script_path ? compiler.script_path : "user/user.ld");
    arguments[position++] = "-o";
    arguments[position++] = (char *)output_path;
    arguments[position++] = (char *)object_path;
    for (size_t i = 0; i < extra_count; ++i) arguments[position++] = extra_objects[i];
    arguments[position] = NULL;
    int result = run_tool(arguments);
    free(arguments);
    if (result != 0) fail("linker failed");
}

void print_help(void) {
    puts("orion-cc - small freestanding C compiler for OrionOS\nUsage: orion-cc [-S|-c] [-o output] [-T script] source.c [input.o ...]\nOptions: -S emit assembly, -c emit object, -T select linker script, -nostart omit the default entry point");
}
