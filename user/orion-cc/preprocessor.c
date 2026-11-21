#include "compiler.h"

static int macro_depth;
static int condition_depth;

typedef struct {
    int parent_active;
    int branch_taken;
    int saw_else;
} ConditionalState;

static ConditionalState conditionals[64];

Macro *find_macro(const char *name) {
    for (size_t i = 0; i < compiler.macro_count; ++i) if (strcmp(compiler.macros[i].name, name) == 0) return &compiler.macros[i];
    return NULL;
}

void define_macro(const char *name, const char *replacement) {
    Macro *existing = find_macro(name);
    if (existing) {
        free(existing->replacement);
        existing->replacement = copy_text(replacement);
        return;
    }
    compiler.macros = array_grow(compiler.macros, &compiler.macro_capacity, compiler.macro_count + 1, sizeof(Macro));
    compiler.macros[compiler.macro_count].name = copy_text(name);
    compiler.macros[compiler.macro_count].replacement = copy_text(replacement);
    ++compiler.macro_count;
}

void expand_line(Buffer *output, const char *line) {
    if (macro_depth > 16) fail("macro expansion is recursive");
    ++macro_depth;
    const char *cursor = line;
    while (*cursor) {
        if (*cursor == '"' || *cursor == '\'') {
            char quote = *cursor;
            buffer_char(output, *cursor++);
            while (*cursor) {
                char value = *cursor++;
                buffer_char(output, value);
                if (value == '\\' && *cursor) buffer_char(output, *cursor++);
                else if (value == quote) break;
            }
            continue;
        }
        if (is_alpha(*cursor)) {
            const char *start = cursor++;
            while (is_alnum(*cursor)) ++cursor;
            char *name = copy_range(start, (size_t)(cursor - start));
            Macro *macro = find_macro(name);
            if (macro) expand_line(output, macro->replacement);
            else buffer_bytes(output, start, (size_t)(cursor - start));
            free(name);
            continue;
        }
        buffer_char(output, *cursor++);
    }
    --macro_depth;
}

static const char *skip_condition_space(const char *text) {
    while (is_space(*text)) ++text;
    return text;
}

static int condition_value(const char *text, unsigned depth) {
    if (depth > 32) fail("preprocessor condition is too complex");
    text = skip_condition_space(text);
    size_t length = strlen(text);
    while (length && is_space(text[length - 1])) --length;
    if (!length) fail("preprocessor condition is empty");
    if (text[0] == '!') return !condition_value(text + 1, depth + 1);
    if (text[0] == '(' && text[length - 1] == ')') {
        unsigned nesting = 0;
        int wraps = 1;
        for (size_t i = 0; i < length; ++i) {
            if (text[i] == '(') ++nesting;
            else if (text[i] == ')') {
                if (!nesting) fail("invalid preprocessor condition");
                --nesting;
                if (!nesting && i + 1 != length) wraps = 0;
            }
        }
        if (wraps && !nesting) {
            char *inner = copy_range(text + 1, length - 2);
            int result = condition_value(inner, depth + 1);
            free(inner);
            return result;
        }
    }
    unsigned nesting = 0;
    for (size_t i = 0; i + 1 < length; ++i) {
        if (text[i] == '(') ++nesting;
        else if (text[i] == ')' && nesting) --nesting;
        else if (!nesting && text[i] == '|' && text[i + 1] == '|') {
            char *left = copy_range(text, i);
            int result = condition_value(left, depth + 1) || condition_value(text + i + 2, depth + 1);
            free(left);
            return result;
        }
    }
    nesting = 0;
    for (size_t i = 0; i + 1 < length; ++i) {
        if (text[i] == '(') ++nesting;
        else if (text[i] == ')' && nesting) --nesting;
        else if (!nesting && text[i] == '&' && text[i + 1] == '&') {
            char *left = copy_range(text, i);
            int result = condition_value(left, depth + 1) && condition_value(text + i + 2, depth + 1);
            free(left);
            return result;
        }
    }
    nesting = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '(') ++nesting;
        else if (text[i] == ')' && nesting) --nesting;
        else if (!nesting && (text[i] == '<' || text[i] == '>')) {
            int operation = text[i];
            size_t width = 1;
            if (i + 1 < length && text[i + 1] == '=') {
                operation = operation == '<' ? 1 : 2;
                width = 2;
            }
            char *left = copy_range(text, i);
            long long left_value = condition_value(left, depth + 1);
            long long right_value = condition_value(text + i + width, depth + 1);
            free(left);
            if (operation == '<') return left_value < right_value;
            if (operation == '>') return left_value > right_value;
            if (operation == 1) return left_value <= right_value;
            return left_value >= right_value;
        } else if (!nesting && (text[i] == '=' || text[i] == '!') && i + 1 < length && text[i + 1] == '=') {
            char *left = copy_range(text, i);
            long long left_value = condition_value(left, depth + 1);
            long long right_value = condition_value(text + i + 2, depth + 1);
            free(left);
            return text[i] == '=' ? left_value == right_value : left_value != right_value;
        }
    }
    if (length >= 7 && strncmp(text, "defined", 7) == 0 && (length == 7 || !is_alnum(text[7]))) {
        const char *name = skip_condition_space(text + 7);
        int parenthesized = *name == '(';
        if (parenthesized) name = skip_condition_space(name + 1);
        const char *end = name;
        if (!is_alpha(*end)) fail("identifier expected after defined");
        while (is_alnum(*end)) ++end;
        char *identifier = copy_range(name, (size_t)(end - name));
        const char *tail = skip_condition_space(end);
        if (parenthesized) {
            if (*tail != ')') fail("closing parenthesis expected after defined");
            tail = skip_condition_space(tail + 1);
        }
        if (*tail) fail("invalid text after defined expression");
        int result = find_macro(identifier) != NULL;
        free(identifier);
        return result;
    }
    if (is_alpha(text[0])) {
        size_t identifier_length = 1;
        while (identifier_length < length && is_alnum(text[identifier_length])) ++identifier_length;
        const char *tail = skip_condition_space(text + identifier_length);
        if (*tail) fail("unsupported preprocessor condition");
        char *identifier = copy_range(text, identifier_length);
        Macro *macro = find_macro(identifier);
        if (!macro) {
            free(identifier);
            return 0;
        }
        if (strcmp(macro->replacement, identifier) == 0) {
            free(identifier);
            return 0;
        }
        free(identifier);
        return condition_value(macro->replacement, depth + 1);
    }
    char *number = copy_range(text, length);
    char *end = NULL;
    long long value = strtol(number, &end, 0);
    if (end == number) {
        free(number);
        fail("unsupported preprocessor condition");
    }
    while (is_space(*end)) ++end;
    if (*end && is_alpha(*end)) {
        while (is_alnum(*end)) ++end;
        while (is_space(*end)) ++end;
    }
    if (*end) {
        free(number);
        fail("unsupported preprocessor condition");
    }
    free(number);
    return value != 0;
}

static void remove_macro(const char *name) {
    for (size_t i = 0; i < compiler.macro_count; ++i) {
        if (strcmp(compiler.macros[i].name, name) == 0) {
            free(compiler.macros[i].name);
            free(compiler.macros[i].replacement);
            compiler.macros[i] = compiler.macros[compiler.macro_count - 1];
            --compiler.macro_count;
            return;
        }
    }
}

char *preprocess(const char *source) {
    Buffer output = {0};
    const char *line = source;
    int active = 1;
    condition_depth = 0;
    define_macro("__STDC__", "1");
    define_macro("__STDC_VERSION__", "199901L");
    define_macro("__ORIONOS__", "1");
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        size_t first = 0;
        while (first < length && (line[first] == ' ' || line[first] == '\t')) ++first;
        if (first < length && line[first] == '#') {
            const char *cursor = line + first + 1;
            const char *limit = line + length;
            while (cursor < limit && is_space(*cursor)) ++cursor;
            const char *word = cursor;
            while (cursor < limit && is_alpha(*cursor)) ++cursor;
            size_t word_length = (size_t)(cursor - word);
            while (cursor < limit && is_space(*cursor)) ++cursor;
            size_t argument_length = (size_t)(limit - cursor);
            char *argument = copy_range(cursor, argument_length);
            if (word_length == 2 && strncmp(word, "if", 2) == 0) {
                if (condition_depth == sizeof(conditionals) / sizeof(conditionals[0])) fail("conditional nesting is too deep");
                int result = active ? condition_value(argument, 0) : 0;
                conditionals[condition_depth++] = (ConditionalState){active, result, 0};
                active = active && result;
            } else if ((word_length == 5 && strncmp(word, "ifdef", 5) == 0) || (word_length == 6 && strncmp(word, "ifndef", 6) == 0)) {
                if (condition_depth == sizeof(conditionals) / sizeof(conditionals[0])) fail("conditional nesting is too deep");
                if (!is_alpha(argument[0])) fail("identifier expected in conditional directive");
                size_t name_length = 1;
                while (is_alnum(argument[name_length])) ++name_length;
                char *name = copy_range(argument, name_length);
                const char *tail = skip_condition_space(argument + name_length);
                if (*tail) fail("unexpected text in conditional directive");
                int result = find_macro(name) != NULL;
                if (word_length == 6) result = !result;
                conditionals[condition_depth++] = (ConditionalState){active, result, 0};
                active = active && result;
                free(name);
            } else if (word_length == 4 && strncmp(word, "elif", 4) == 0) {
                if (!condition_depth || conditionals[condition_depth - 1].saw_else) fail("unexpected elif directive");
                ConditionalState *state = &conditionals[condition_depth - 1];
                int result = state->parent_active && !state->branch_taken ? condition_value(argument, 0) : 0;
                active = state->parent_active && !state->branch_taken && result;
                state->branch_taken = state->branch_taken || result;
            } else if (word_length == 4 && strncmp(word, "else", 4) == 0) {
                if (!condition_depth || conditionals[condition_depth - 1].saw_else) fail("unexpected else directive");
                ConditionalState *state = &conditionals[condition_depth - 1];
                state->saw_else = 1;
                active = state->parent_active && !state->branch_taken;
                state->branch_taken = 1;
            } else if (word_length == 5 && strncmp(word, "endif", 5) == 0) {
                if (!condition_depth) fail("unexpected endif directive");
                active = conditionals[--condition_depth].parent_active;
            } else if (active && word_length == 6 && strncmp(word, "define", 6) == 0) {
                const char *name = cursor;
                while (cursor < limit && is_alnum(*cursor)) ++cursor;
                if (cursor == name) fail("macro name is missing");
                if (cursor < limit && *cursor == '(') fail("function-like macros are not supported");
                char *macro_name = copy_range(name, (size_t)(cursor - name));
                while (cursor < limit && is_space(*cursor)) ++cursor;
                char *replacement = copy_range(cursor, (size_t)(limit - cursor));
                define_macro(macro_name, *replacement ? replacement : "1");
                free(macro_name);
                free(replacement);
            } else if (active && word_length == 5 && strncmp(word, "undef", 5) == 0) {
                const char *name = argument;
                if (!is_alpha(*name)) fail("identifier expected after undef");
                size_t name_length = 1;
                while (is_alnum(name[name_length])) ++name_length;
                const char *tail = skip_condition_space(name + name_length);
                if (*tail) fail("unexpected text after undef name");
                char *macro_name = copy_range(name, name_length);
                remove_macro(macro_name);
                free(macro_name);
            } else if (active && !((word_length == 7 && strncmp(word, "include", 7) == 0) || (word_length == 6 && strncmp(word, "pragma", 6) == 0))) fail("unsupported preprocessor directive");
            free(argument);
        } else if (active) {
            char *plain = copy_range(line, length);
            expand_line(&output, plain);
            free(plain);
            buffer_char(&output, '\n');
        }
        line = end ? end + 1 : line + length;
    }
    if (condition_depth) fail("unterminated conditional directive");
    buffer_char(&output, 0);
    return output.data;
}
