#include "as.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    int parameters;
    char *body;
} AsMacro;

typedef struct {
    char *name;
    int64_t value;
} AsVariable;

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} TextBuffer;

typedef struct {
    char **lines;
    size_t line_count;
    AsMacro *macros;
    size_t macro_count;
    size_t macro_capacity;
    AsVariable *variables;
    size_t variable_count;
    size_t variable_capacity;
} Preprocessor;

typedef struct {
    const char *cursor;
} IntegerParser;

static void text_append(TextBuffer *buffer, const char *text, size_t length) {
    if (buffer->size > 16 * 1024 * 1024 || length > 16 * 1024 * 1024 - buffer->size) as_fail("expanded source is too large");
    if (length > (size_t)-1 - buffer->size - 1) as_fail("source is too large");
    size_t needed = buffer->size + length + 1;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 512;
        while (capacity < needed) {
            if (capacity > (size_t)-1 / 2) as_fail("source is too large");
            capacity *= 2;
        }
        buffer->data = as_grow(buffer->data, buffer->capacity, capacity);
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->size, text, length);
    buffer->size += length;
    buffer->data[buffer->size] = 0;
}

static char *trim_line(char *line) {
    while (*line == ' ' || *line == '\t' || *line == '\r') ++line;
    size_t length = strlen(line);
    while (length && (line[length - 1] == ' ' || line[length - 1] == '\t' || line[length - 1] == '\r')) line[--length] = 0;
    return line;
}

static int identifier_character(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_';
}

static int variable_find(Preprocessor *preprocessor, const char *name, int create) {
    for (size_t i = 0; i < preprocessor->variable_count; ++i) if (strcmp(preprocessor->variables[i].name, name) == 0) return (int)i;
    if (!create) return -1;
    if (preprocessor->variable_count == preprocessor->variable_capacity) {
        size_t capacity = preprocessor->variable_capacity ? preprocessor->variable_capacity * 2 : 8;
        preprocessor->variables = as_grow(preprocessor->variables, preprocessor->variable_capacity * sizeof(AsVariable), capacity * sizeof(AsVariable));
        preprocessor->variable_capacity = capacity;
    }
    size_t index = preprocessor->variable_count++;
    preprocessor->variables[index].name = as_duplicate(name);
    preprocessor->variables[index].value = 0;
    return (int)index;
}

static int macro_find(Preprocessor *preprocessor, const char *name) {
    for (size_t i = 0; i < preprocessor->macro_count; ++i) if (strcmp(preprocessor->macros[i].name, name) == 0) return (int)i;
    return -1;
}

static int64_t parse_or(IntegerParser *parser);

static void integer_space(IntegerParser *parser) {
    while (*parser->cursor == ' ' || *parser->cursor == '\t') ++parser->cursor;
}

static int64_t parse_integer_primary(IntegerParser *parser) {
    integer_space(parser);
    if (*parser->cursor == '(') {
        ++parser->cursor;
        int64_t result = parse_or(parser);
        integer_space(parser);
        if (*parser->cursor != ')') as_fail("invalid preprocessor expression");
        ++parser->cursor;
        return result;
    }
    if (*parser->cursor == '+' || *parser->cursor == '-') {
        int negative = *parser->cursor == '-';
        ++parser->cursor;
        int64_t value = parse_integer_primary(parser);
        return negative ? -value : value;
    }
    char *end;
    long value = strtol(parser->cursor, &end, 0);
    if (end == parser->cursor) as_fail("invalid preprocessor expression");
    parser->cursor = end;
    return value;
}

static int64_t parse_integer_sum(IntegerParser *parser) {
    int64_t value = parse_integer_primary(parser);
    for (;;) {
        integer_space(parser);
        if (*parser->cursor != '+' && *parser->cursor != '-') return value;
        char operation = *parser->cursor++;
        int64_t next = parse_integer_primary(parser);
        value = operation == '+' ? value + next : value - next;
    }
}

static int64_t parse_equal(IntegerParser *parser) {
    int64_t value = parse_integer_sum(parser);
    for (;;) {
        integer_space(parser);
        if (parser->cursor[0] == '=' && parser->cursor[1] == '=') {
            parser->cursor += 2;
            value = value == parse_integer_sum(parser);
        } else if (parser->cursor[0] == '!' && parser->cursor[1] == '=') {
            parser->cursor += 2;
            value = value != parse_integer_sum(parser);
        } else return value;
    }
}

static int64_t parse_and(IntegerParser *parser) {
    int64_t value = parse_equal(parser);
    for (;;) {
        integer_space(parser);
        if (parser->cursor[0] != '&' || parser->cursor[1] != '&') return value;
        parser->cursor += 2;
        int64_t next = parse_equal(parser);
        value = value && next;
    }
}

static int64_t parse_or(IntegerParser *parser) {
    int64_t value = parse_and(parser);
    for (;;) {
        integer_space(parser);
        if (parser->cursor[0] != '|' || parser->cursor[1] != '|') return value;
        parser->cursor += 2;
        int64_t next = parse_and(parser);
        value = value || next;
    }
}

static int64_t eval_expression(Preprocessor *preprocessor, const char *text) {
    TextBuffer expanded = {0};
    for (size_t i = 0; text[i];) {
        if ((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z') || text[i] == '_') {
            char name[128];
            size_t length = 0;
            while (identifier_character(text[i])) {
                if (length + 1 >= sizeof(name)) as_fail("preprocessor variable name is too long");
                name[length++] = text[i++];
            }
            name[length] = 0;
            int index = variable_find(preprocessor, name, 0);
            if (index < 0) as_fail("unknown preprocessor variable");
            char number[32];
            size_t number_length = 0;
            uint64_t value = (uint64_t)preprocessor->variables[index].value;
            if ((int64_t)value < 0) {
                number[number_length++] = '-';
                value = (uint64_t)(-(preprocessor->variables[index].value + 1)) + 1;
            }
            char reverse[24];
            size_t digits = 0;
            do { reverse[digits++] = (char)('0' + value % 10); value /= 10; } while (value);
            while (digits) number[number_length++] = reverse[--digits];
            number[number_length] = 0;
            text_append(&expanded, number, number_length);
        } else {
            text_append(&expanded, text + i, 1);
            ++i;
        }
    }
    IntegerParser parser = {expanded.data ? expanded.data : "0"};
    int64_t value = parse_or(&parser);
    integer_space(&parser);
    if (*parser.cursor) as_fail("invalid preprocessor expression");
    free(expanded.data);
    return value;
}

static char *replace_variables(Preprocessor *preprocessor, const char *line) {
    TextBuffer output = {0};
    for (size_t i = 0; line[i];) {
        if ((line[i] >= 'a' && line[i] <= 'z') || (line[i] >= 'A' && line[i] <= 'Z') || line[i] == '_') {
            char name[128];
            size_t length = 0;
            while (identifier_character(line[i])) {
                if (length + 1 >= sizeof(name)) as_fail("preprocessor variable name is too long");
                name[length++] = line[i++];
            }
            name[length] = 0;
            int index = variable_find(preprocessor, name, 0);
            if (index < 0) text_append(&output, name, length);
            else {
                char number[32];
                size_t used = 0;
                int64_t signed_value = preprocessor->variables[index].value;
                uint64_t value = (uint64_t)signed_value;
                if (signed_value < 0) {
                    number[used++] = '-';
                    value = (uint64_t)(-(signed_value + 1)) + 1;
                }
                char reverse[24];
                size_t digits = 0;
                do { reverse[digits++] = (char)('0' + value % 10); value /= 10; } while (value);
                while (digits) number[used++] = reverse[--digits];
                text_append(&output, number, used);
            }
        } else {
            text_append(&output, line + i, 1);
            ++i;
        }
    }
    if (!output.data) return as_duplicate("");
    return output.data;
}

static size_t find_end(Preprocessor *preprocessor, size_t start, const char *opening, const char *closing, size_t *else_line) {
    int depth = 0;
    if (else_line) *else_line = (size_t)-1;
    for (size_t i = start + 1; i < preprocessor->line_count; ++i) {
        char *line = trim_line(preprocessor->lines[i]);
        if (strncmp(line, opening, strlen(opening)) == 0 && (line[strlen(opening)] == 0 || line[strlen(opening)] == ' ' || line[strlen(opening)] == '\t')) ++depth;
        if (strncmp(line, closing, strlen(closing)) == 0 && (line[strlen(closing)] == 0 || line[strlen(closing)] == ' ' || line[strlen(closing)] == '\t')) {
            if (!depth) return i;
            --depth;
        }
        if (else_line && !depth && strncmp(line, "%else", 5) == 0 && (line[5] == 0 || line[5] == ' ' || line[5] == '\t')) *else_line = i;
    }
    as_fail("unterminated preprocessor block");
    return 0;
}

static char *substitute_parameters(const char *body, char **arguments, int count) {
    TextBuffer output = {0};
    for (size_t i = 0; body[i];) {
        if (body[i] == '%' && body[i + 1] >= '1' && body[i + 1] <= '9') {
            int index = body[i + 1] - '1';
            if (index >= count) as_fail("macro argument is missing");
            const char *argument = arguments[index];
            text_append(&output, argument, strlen(argument));
            i += 2;
        } else {
            text_append(&output, body + i, 1);
            ++i;
        }
    }
    return output.data ? output.data : as_duplicate("");
}

static char **split_lines(char *source, size_t *count) {
    size_t lines = 1;
    for (char *p = source; *p; ++p) if (*p == '\n') ++lines;
    char **result = malloc(lines * sizeof(char *));
    if (!result) as_fail("out of memory");
    size_t used = 0;
    char *line = source;
    for (char *p = source;; ++p) {
        if (*p == '\r') *p = 0;
        if (*p == '\n' || *p == 0) {
            int end = *p == 0;
            *p = 0;
            result[used++] = line;
            if (end) break;
            line = p + 1;
        }
    }
    *count = used;
    return result;
}

static void process_range(Preprocessor *preprocessor, size_t start, size_t end, TextBuffer *output, int depth);

static int parse_macro_arguments(char *text, char **arguments, int capacity) {
    int count = 0;
    char *cursor = trim_line(text);
    while (*cursor) {
        if (count >= capacity) as_fail("too many macro arguments");
        char *begin = cursor;
        int nesting = 0;
        while (*cursor) {
            if (*cursor == '[' || *cursor == '(') ++nesting;
            if (*cursor == ']' || *cursor == ')') --nesting;
            if (*cursor == ',' && !nesting) break;
            ++cursor;
        }
        if (*cursor) *cursor++ = 0;
        arguments[count++] = trim_line(begin);
        cursor = trim_line(cursor);
    }
    return count;
}

static void process_range(Preprocessor *preprocessor, size_t start, size_t end, TextBuffer *output, int depth) {
    if (depth > 16) as_fail("preprocessor nesting is too deep");
    for (size_t i = start; i < end; ++i) {
        char *raw = preprocessor->lines[i];
        char *line = trim_line(raw);
        if (!*line) {
            text_append(output, "\n", 1);
            continue;
        }
        if (strncmp(line, "%macro", 6) == 0 && (line[6] == ' ' || line[6] == '\t')) {
            size_t close = find_end(preprocessor, i, "%macro", "%endmacro", NULL);
            char *declaration = trim_line(line + 6);
            char *split = declaration;
            while (*split && *split != ' ' && *split != '\t') ++split;
            if (!*split) as_fail("macro parameter count is missing");
            *split++ = 0;
            char *count_text = trim_line(split);
            int parameters = (int)eval_expression(preprocessor, count_text);
            if (parameters < 0 || parameters > 9) as_fail("unsupported macro parameter count");
            if (preprocessor->macro_count == preprocessor->macro_capacity) {
                size_t capacity = preprocessor->macro_capacity ? preprocessor->macro_capacity * 2 : 8;
                preprocessor->macros = as_grow(preprocessor->macros, preprocessor->macro_capacity * sizeof(AsMacro), capacity * sizeof(AsMacro));
                preprocessor->macro_capacity = capacity;
            }
            AsMacro *macro = &preprocessor->macros[preprocessor->macro_count++];
            macro->name = as_duplicate(declaration);
            macro->parameters = parameters;
            TextBuffer body = {0};
            for (size_t j = i + 1; j < close; ++j) {
                text_append(&body, preprocessor->lines[j], strlen(preprocessor->lines[j]));
                text_append(&body, "\n", 1);
            }
            macro->body = body.data ? body.data : as_duplicate("");
            i = close;
            continue;
        }
        if (strncmp(line, "%assign", 7) == 0 && (line[7] == ' ' || line[7] == '\t')) {
            char *assignment = as_duplicate(trim_line(line + 7));
            char *cursor = assignment;
            while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
            if (!*cursor) as_fail("invalid preprocessor assignment");
            *cursor++ = 0;
            int index = variable_find(preprocessor, assignment, 1);
            preprocessor->variables[index].value = eval_expression(preprocessor, trim_line(cursor));
            free(assignment);
            continue;
        }
        if (strncmp(line, "%rep", 4) == 0 && (line[4] == ' ' || line[4] == '\t')) {
            size_t close = find_end(preprocessor, i, "%rep", "%endrep", NULL);
            int64_t repetitions = eval_expression(preprocessor, trim_line(line + 4));
            if (repetitions < 0 || repetitions > 4096) as_fail("invalid repetition count");
            for (int64_t repeat = 0; repeat < repetitions; ++repeat) process_range(preprocessor, i + 1, close, output, depth + 1);
            i = close;
            continue;
        }
        if (strncmp(line, "%if", 3) == 0 && (line[3] == ' ' || line[3] == '\t')) {
            size_t else_line;
            size_t close = find_end(preprocessor, i, "%if", "%endif", &else_line);
            int condition = eval_expression(preprocessor, trim_line(line + 3)) != 0;
            if (condition) process_range(preprocessor, i + 1, else_line == (size_t)-1 ? close : else_line, output, depth + 1);
            else if (else_line != (size_t)-1) process_range(preprocessor, else_line + 1, close, output, depth + 1);
            i = close;
            continue;
        }
        char *expanded = replace_variables(preprocessor, line);
        char *original = as_duplicate(expanded);
        char *cursor = expanded;
        while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
        char *rest = cursor;
        if (*cursor) *cursor++ = 0;
        rest = trim_line(cursor);
        int macro_index = macro_find(preprocessor, expanded);
        if (macro_index >= 0) {
            char *arguments[9];
            int count = parse_macro_arguments(rest, arguments, 9);
            AsMacro *macro = &preprocessor->macros[macro_index];
            if (count != macro->parameters) as_fail("incorrect macro argument count");
            char *body = substitute_parameters(macro->body, arguments, count);
            size_t body_count;
            char **body_lines = split_lines(body, &body_count);
            char **saved_lines = preprocessor->lines;
            size_t saved_count = preprocessor->line_count;
            preprocessor->lines = body_lines;
            preprocessor->line_count = body_count;
            process_range(preprocessor, 0, body_count, output, depth + 1);
            preprocessor->lines = saved_lines;
            preprocessor->line_count = saved_count;
            free(body_lines);
            free(body);
        } else {
            text_append(output, original, strlen(original));
            text_append(output, "\n", 1);
        }
        free(original);
        free(expanded);
    }
}

char *as_preprocess(char *source) {
    Preprocessor preprocessor;
    memset(&preprocessor, 0, sizeof(preprocessor));
    preprocessor.lines = split_lines(source, &preprocessor.line_count);
    TextBuffer output = {0};
    process_range(&preprocessor, 0, preprocessor.line_count, &output, 0);
    free(preprocessor.lines);
    for (size_t i = 0; i < preprocessor.macro_count; ++i) {
        free(preprocessor.macros[i].name);
        free(preprocessor.macros[i].body);
    }
    for (size_t i = 0; i < preprocessor.variable_count; ++i) free(preprocessor.variables[i].name);
    free(preprocessor.macros);
    free(preprocessor.variables);
    return output.data ? output.data : as_duplicate("");
}
