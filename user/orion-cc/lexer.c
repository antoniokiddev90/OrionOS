#include "compiler.h"

int decode_escape(const char **cursor) {
    int value = (unsigned char)*(*cursor)++;
    if (value != '\\') return value;
    value = (unsigned char)*(*cursor)++;
    if (value == 'n') return '\n';
    if (value == 'r') return '\r';
    if (value == 't') return '\t';
    if (value == 'b') return '\b';
    if (value == 'f') return '\f';
    if (value == 'v') return '\v';
    if (value == 'a') return '\a';
    if (value == 'x') {
        int result = 0;
        int digits = 0;
        while (is_digit(**cursor) || (**cursor >= 'a' && **cursor <= 'f') || (**cursor >= 'A' && **cursor <= 'F')) {
            char c = *(*cursor)++;
            int digit = is_digit(c) ? c - '0' : c >= 'a' ? c - 'a' + 10 : c - 'A' + 10;
            result = result * 16 + digit;
            ++digits;
        }
        if (!digits) fail("invalid hexadecimal escape");
        return result & 255;
    }
    if (value >= '0' && value <= '7') {
        int result = value - '0';
        for (int i = 1; i < 3 && *(*cursor) >= '0' && *(*cursor) <= '7'; ++i) result = result * 8 + (*(*cursor)++ - '0');
        return result & 255;
    }
    return value;
}

void add_token(int kind, const char *text, size_t length, long long value, unsigned line) {
    compiler.tokens = array_grow(compiler.tokens, &compiler.token_capacity, compiler.token_count + 1, sizeof(Token));
    Token *token = &compiler.tokens[compiler.token_count++];
    token->kind = kind;
    token->text = copy_range(text, length);
    token->length = length;
    token->value = value;
    token->line = line;
}

long long parse_integer(const char *text, size_t length) {
    char *number = copy_range(text, length);
    size_t size = strlen(number);
    while (size && (number[size - 1] == 'u' || number[size - 1] == 'U' || number[size - 1] == 'l' || number[size - 1] == 'L')) number[--size] = 0;
    long long value = 0;
    if (number[0] == '0' && (number[1] == 'b' || number[1] == 'B')) {
        for (size_t i = 2; number[i] == '0' || number[i] == '1'; ++i) value = value * 2 + number[i] - '0';
    } else value = strtol(number, NULL, 0);
    free(number);
    return value;
}

int match_operator(const char *cursor, const char **text, int *kind) {
    static const struct { const char *text; int kind; } operators[] = {
        {"<<=", OP_SHL_ASSIGN}, {">>=", OP_SHR_ASSIGN}, {"==", OP_EQ}, {"!=", OP_NE}, {"<=", OP_LE}, {">=", OP_GE},
        {"<<", OP_SHL}, {">>", OP_SHR}, {"&&", OP_ANDAND}, {"||", OP_OROR}, {"+=", OP_ADD_ASSIGN}, {"-=", OP_SUB_ASSIGN},
        {"*=", OP_MUL_ASSIGN}, {"/=", OP_DIV_ASSIGN}, {"%=", OP_MOD_ASSIGN}, {"&=", OP_AND_ASSIGN}, {"|=", OP_OR_ASSIGN},
        {"^=", OP_XOR_ASSIGN}, {"++", OP_INC}, {"--", OP_DEC}
    };
    for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i) {
        size_t length = strlen(operators[i].text);
        if (strncmp(cursor, operators[i].text, length) == 0) {
            *text = operators[i].text;
            *kind = operators[i].kind;
            return (int)length;
        }
    }
    return 0;
}

void tokenize(const char *source) {
    const char *cursor = source;
    unsigned line = 1;
    while (*cursor) {
        if (is_space(*cursor)) {
            if (*cursor++ == '\n') ++line;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '/') {
            while (*cursor && *cursor != '\n') ++cursor;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/')) {
                if (*cursor++ == '\n') ++line;
            }
            if (*cursor) cursor += 2;
            continue;
        }
        if (is_alpha(*cursor)) {
            const char *start = cursor++;
            while (is_alnum(*cursor)) ++cursor;
            add_token(T_IDENTIFIER, start, (size_t)(cursor - start), 0, line);
            continue;
        }
        if (is_digit(*cursor)) {
            const char *start = cursor++;
            while (is_alnum(*cursor)) ++cursor;
            add_token(T_NUMBER, start, (size_t)(cursor - start), parse_integer(start, (size_t)(cursor - start)), line);
            continue;
        }
        if (*cursor == '"') {
            ++cursor;
            Buffer string = {0};
            while (*cursor && *cursor != '"') {
                if (*cursor == '\n') fail("newline in string literal");
                int value = decode_escape(&cursor);
                buffer_char(&string, (char)value);
            }
            if (*cursor != '"') fail("unterminated string literal");
            ++cursor;
            add_token(T_STRING, string.data ? string.data : "", string.size, 0, line);
            free(string.data);
            continue;
        }
        if (*cursor == '\'') {
            ++cursor;
            if (!*cursor || *cursor == '\n') fail("unterminated character constant");
            int value = decode_escape(&cursor);
            if (*cursor != '\'') fail("character constant must contain one character");
            ++cursor;
            add_token(T_NUMBER, "", 0, value, line);
            continue;
        }
        const char *operator_text = NULL;
        int operator_kind = 0;
        int operator_length = match_operator(cursor, &operator_text, &operator_kind);
        if (operator_length) {
            add_token(operator_kind, operator_text, (size_t)operator_length, 0, line);
            cursor += operator_length;
            continue;
        }
        if (strchr("(){}[];,:?~!+-*/%&|^<>=", *cursor)) {
            add_token((unsigned char)*cursor, cursor, 1, 0, line);
            ++cursor;
            continue;
        }
        fail("invalid character in source");
    }
    add_token(T_EOF, "", 0, 0, line);
}

Token *peek(void) {
    return &compiler.tokens[compiler.position];
}

int accept(int kind) {
    if (peek()->kind != kind) return 0;
    ++compiler.position;
    return 1;
}

int accept_word(const char *word) {
    if (peek()->kind != T_IDENTIFIER || strcmp(peek()->text, word) != 0) return 0;
    ++compiler.position;
    return 1;
}

void expect(int kind, const char *message) {
    if (!accept(kind)) fail(message);
}

char *expect_identifier(void) {
    if (peek()->kind != T_IDENTIFIER) fail("identifier expected");
    return copy_text(compiler.tokens[compiler.position++].text);
}

