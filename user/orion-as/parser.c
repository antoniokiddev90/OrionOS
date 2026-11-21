#include "as.h"
#include <stdlib.h>
#include <string.h>

char *as_preprocess(char *source);

static char *trim_text(char *text) {
    while (*text == ' ' || *text == '\t' || *text == '\r') ++text;
    size_t length = strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r')) text[--length] = 0;
    return text;
}

static char *strip_comment(char *line) {
    int quoted = 0;
    for (char *cursor = line; *cursor; ++cursor) {
        if (*cursor == '"' && (cursor == line || cursor[-1] != '\\')) quoted = !quoted;
        if (!quoted && (*cursor == ';' || *cursor == '#')) {
            *cursor = 0;
            break;
        }
    }
    return trim_text(line);
}

static size_t position_of(Assembler *assembler) {
    AsSection *section = &assembler->sections[assembler->current_section];
    return section->type == 8 ? (size_t)section->bss_size : section->bytes.size;
}

static int split_operands(char *text, char **parts, int capacity) {
    int count = 0;
    char *start = text;
    int bracket = 0;
    int parenthesis = 0;
    int quoted = 0;
    for (char *cursor = text;; ++cursor) {
        if (*cursor == '"' && (cursor == text || cursor[-1] != '\\')) quoted = !quoted;
        if (!quoted) {
            if (*cursor == '[') ++bracket;
            if (*cursor == ']') --bracket;
            if (*cursor == '(') ++parenthesis;
            if (*cursor == ')') --parenthesis;
        }
        if ((!*cursor || (*cursor == ',' && !bracket && !parenthesis && !quoted))) {
            if (count >= capacity) as_fail("too many operands");
            int at_end = !*cursor;
            if (!at_end) *cursor = 0;
            parts[count++] = trim_text(start);
            if (at_end) break;
            start = cursor + 1;
        }
    }
    return count;
}

static void emit_expression(Assembler *assembler, char *text, size_t width) {
    int64_t value;
    int symbol;
    int64_t addend;
    int result = as_expression(assembler, text, &value, &symbol, &addend);
    if (result != 0) as_fail("invalid expression");
    if (symbol >= 0) {
        uint32_t relocation_type = width == 8 ? 1 : width == 4 ? 10 : 0;
        if (!relocation_type) as_fail("unsupported data relocation width");
        if (assembler->pass == 2) {
            AsRelocation relocation;
            if (assembler->relocation_count == assembler->relocation_capacity) {
                size_t capacity = assembler->relocation_capacity ? assembler->relocation_capacity * 2 : 32;
                assembler->relocations = as_grow(assembler->relocations, assembler->relocation_capacity * sizeof(AsRelocation), capacity * sizeof(AsRelocation));
                assembler->relocation_capacity = capacity;
            }
            relocation.section = assembler->current_section;
            relocation.offset = position_of(assembler);
            relocation.symbol = (uint32_t)symbol;
            relocation.type = relocation_type;
            relocation.addend = addend;
            assembler->relocations[assembler->relocation_count++] = relocation;
        }
        as_emit_zeros(assembler, width);
    } else {
        as_emit_integer(assembler, (uint64_t)value, width);
    }
}

static void parse_data(Assembler *assembler, char *operands, size_t width) {
    char *parts[128];
    int count = split_operands(operands, parts, 128);
    for (int i = 0; i < count; ++i) {
        char *part = parts[i];
        size_t length = strlen(part);
        if (width == 1 && length >= 2 && part[0] == '"' && part[length - 1] == '"') {
            for (size_t j = 1; j + 1 < length; ++j) {
                unsigned char value = (unsigned char)part[j];
                if (value == '\\' && j + 2 < length) {
                    char escaped = part[++j];
                    value = escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : escaped == 't' ? '\t' : (unsigned char)escaped;
                }
                as_emit_byte(assembler, value);
            }
        } else {
            emit_expression(assembler, part, width);
        }
    }
}

static char *first_word(char *text, char **rest) {
    text = trim_text(text);
    char *cursor = text;
    while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (*cursor) *cursor++ = 0;
    *rest = trim_text(cursor);
    return text;
}

static void parse_directive(Assembler *assembler, char *line) {
    char *rest;
    char *directive = first_word(line, &rest);
    if (strcmp(directive, "bits") == 0) {
        int64_t value;
        int symbol;
        int64_t addend;
        if (as_expression(assembler, rest, &value, &symbol, &addend) != 0 || symbol >= 0 || (value != 16 && value != 32 && value != 64)) as_fail("unsupported instruction mode");
        assembler->mode_bits = (uint32_t)value;
        return;
    }
    if (strcmp(directive, "default") == 0) return;
    if (strcmp(directive, "section") == 0) {
        char *name = rest;
        while (*name && *name != ',' && *name != ' ' && *name != '\t') ++name;
        *name = 0;
        if (!*rest) as_fail("section name is missing");
        int section = as_section_find(assembler, rest, 1);
        assembler->current_section = (uint32_t)section;
        return;
    }
    if (strcmp(directive, "global") == 0 || strcmp(directive, "extern") == 0) {
        char *names[64];
        int count = split_operands(rest, names, 64);
        for (int i = 0; i < count; ++i) {
            char *name = trim_text(names[i]);
            if (!*name) continue;
            if (name[0] == '.') as_fail("local symbols cannot be global");
            int index = as_symbol_find(assembler, name, 1);
            assembler->symbols[index].binding = 1;
        }
        return;
    }
    if (strcmp(directive, "align") == 0 || strcmp(directive, "alignb") == 0) {
        int64_t value;
        int symbol;
        int64_t addend;
        if (as_expression(assembler, rest, &value, &symbol, &addend) != 0 || symbol >= 0 || value <= 0) as_fail("invalid alignment");
        as_align(assembler, (uint64_t)value);
        return;
    }
    size_t width = 0;
    if (strcmp(directive, "resb") == 0) width = 1;
    else if (strcmp(directive, "resw") == 0) width = 2;
    else if (strcmp(directive, "resd") == 0) width = 4;
    else if (strcmp(directive, "resq") == 0) width = 8;
    if (width) {
        int64_t count;
        int symbol;
        int64_t addend;
        if (as_expression(assembler, rest, &count, &symbol, &addend) != 0 || symbol >= 0 || count < 0 || (uint64_t)count > 0x1000000 / width) as_fail("invalid reservation size");
        AsSection *section = &assembler->sections[assembler->current_section];
        if (section->type != 8) as_fail("reservation directives require a bss section");
        uint64_t bytes = (uint64_t)count * width;
        if (bytes > (uint64_t)-1 - section->bss_size) as_fail("section is too large");
        section->bss_size += bytes;
        return;
    }
    if (strcmp(directive, "db") == 0) parse_data(assembler, rest, 1);
    else if (strcmp(directive, "dw") == 0) parse_data(assembler, rest, 2);
    else if (strcmp(directive, "dd") == 0) parse_data(assembler, rest, 4);
    else if (strcmp(directive, "dq") == 0) parse_data(assembler, rest, 8);
    else as_fail("unsupported directive");
}

static void parse_pass(Assembler *assembler, char *source) {
    char *line = source;
    while (line && *line) {
        char *end = strchr(line, '\n');
        if (end) *end = 0;
        char *text = strip_comment(line);
        if (*text) {
            char *colon = strchr(text, ':');
            if (colon) {
                char *space = strchr(text, ' ');
                if (!space || colon < space) {
                    *colon = 0;
                    as_symbol_define(assembler, trim_text(text));
                    text = trim_text(colon + 1);
                }
            }
            if (*text) {
                if (text[0] == '.') {
                    parse_directive(assembler, text);
                } else {
                    char *copy = as_duplicate(text);
                    char *word_rest;
                    char *word = first_word(copy, &word_rest);
                    if (strcmp(word, "bits") == 0 || strcmp(word, "default") == 0 || strcmp(word, "section") == 0 || strcmp(word, "global") == 0 || strcmp(word, "extern") == 0 || strcmp(word, "align") == 0 || strcmp(word, "alignb") == 0 || strcmp(word, "resb") == 0 || strcmp(word, "resw") == 0 || strcmp(word, "resd") == 0 || strcmp(word, "resq") == 0 || strcmp(word, "db") == 0 || strcmp(word, "dw") == 0 || strcmp(word, "dd") == 0 || strcmp(word, "dq") == 0) {
                        free(copy);
                        copy = as_duplicate(text);
                        parse_directive(assembler, copy);
                    } else {
                        free(copy);
                        copy = as_duplicate(text);
                        as_encode_instruction(assembler, copy);
                    }
                    free(copy);
                }
            }
        }
        line = end ? end + 1 : NULL;
    }
}

void as_parse_source(Assembler *assembler, char *source) {
    char *expanded = as_preprocess(source);
    assembler->pass = 1;
    assembler->current_section = 0;
    assembler->mode_bits = 64;
    char *pass_source = as_duplicate(expanded);
    parse_pass(assembler, pass_source);
    free(pass_source);
    for (size_t i = 0; i < assembler->section_count; ++i) {
        assembler->sections[i].bytes.size = 0;
        assembler->sections[i].bss_size = 0;
    }
    assembler->current_section = 0;
    assembler->mode_bits = 64;
    assembler->pass = 2;
    assembler->relocation_count = 0;
    free(assembler->local_scope);
    assembler->local_scope = NULL;
    pass_source = as_duplicate(expanded);
    parse_pass(assembler, pass_source);
    free(pass_source);
    free(assembler->local_scope);
    assembler->local_scope = NULL;
    free(expanded);
}
