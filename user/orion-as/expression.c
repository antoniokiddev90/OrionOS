#include "as.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    int64_t value;
    int symbol;
    int coefficient;
} AsExpression;

typedef struct {
    Assembler *assembler;
    const char *cursor;
    int failed;
} ExpressionParser;

static void skip_space(ExpressionParser *parser) {
    while (*parser->cursor == ' ' || *parser->cursor == '\t') ++parser->cursor;
}

static uint64_t current_position(Assembler *assembler) {
    AsSection *section = &assembler->sections[assembler->current_section];
    return section->type == 8 ? section->bss_size : section->bytes.size;
}

static AsExpression parse_sum(ExpressionParser *parser);

static AsExpression parse_primary(ExpressionParser *parser) {
    skip_space(parser);
    AsExpression result = {0, -1, 0};
    if (*parser->cursor == '+' || *parser->cursor == '-') {
        int negative = *parser->cursor == '-';
        ++parser->cursor;
        result = parse_primary(parser);
        if (negative) {
            result.value = -result.value;
            result.coefficient = -result.coefficient;
        }
        return result;
    }
    if (*parser->cursor == '(') {
        ++parser->cursor;
        result = parse_sum(parser);
        skip_space(parser);
        if (*parser->cursor != ')') parser->failed = 1;
        else ++parser->cursor;
        return result;
    }
    if (*parser->cursor == '$') {
        ++parser->cursor;
        if (*parser->cursor == '$') {
            ++parser->cursor;
            result.value = 0;
        } else {
            result.value = (int64_t)current_position(parser->assembler);
        }
        return result;
    }
    if ((*parser->cursor >= '0' && *parser->cursor <= '9')) {
        const char *start = parser->cursor;
        int base = 10;
        if (start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
            base = 16;
            parser->cursor += 2;
            start = parser->cursor;
            while ((*parser->cursor >= '0' && *parser->cursor <= '9') || (*parser->cursor >= 'a' && *parser->cursor <= 'f') || (*parser->cursor >= 'A' && *parser->cursor <= 'F')) ++parser->cursor;
            if (parser->cursor == start) parser->failed = 1;
            for (const char *p = start; p < parser->cursor; ++p) {
                int digit = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : *p - 'A' + 10;
                result.value = result.value * base + digit;
            }
            return result;
        }
        if (start[0] == '0' && (start[1] == 'b' || start[1] == 'B')) {
            parser->cursor += 2;
            start = parser->cursor;
            while (*parser->cursor == '0' || *parser->cursor == '1') ++parser->cursor;
            if (parser->cursor == start) parser->failed = 1;
            for (const char *p = start; p < parser->cursor; ++p) result.value = result.value * 2 + (*p - '0');
            return result;
        }
        if (start[0] == '0' && start[1] >= '0' && start[1] <= '7') base = 8;
        while (*parser->cursor >= '0' && *parser->cursor <= '9') ++parser->cursor;
        for (const char *p = start; p < parser->cursor; ++p) {
            int digit = *p - '0';
            if (digit >= base) parser->failed = 1;
            result.value = result.value * base + digit;
        }
        return result;
    }
    if ((*parser->cursor >= 'A' && *parser->cursor <= 'Z') || (*parser->cursor >= 'a' && *parser->cursor <= 'z') || *parser->cursor == '_' || *parser->cursor == '.') {
        char name[256];
        size_t length = 0;
        while ((*parser->cursor >= 'A' && *parser->cursor <= 'Z') || (*parser->cursor >= 'a' && *parser->cursor <= 'z') || (*parser->cursor >= '0' && *parser->cursor <= '9') || *parser->cursor == '_' || *parser->cursor == '.' || *parser->cursor == '$') {
            if (length + 1 >= sizeof(name)) parser->failed = 1;
            else name[length++] = *parser->cursor;
            ++parser->cursor;
        }
        name[length] = 0;
        if (name[0] == '.' && parser->assembler->local_scope) {
            char qualified[512];
            size_t scope_length = strlen(parser->assembler->local_scope);
            if (scope_length + length + 1 > sizeof(qualified)) parser->failed = 1;
            else {
                memcpy(qualified, parser->assembler->local_scope, scope_length);
                memcpy(qualified + scope_length, name, length + 1);
                result.symbol = as_symbol_find(parser->assembler, qualified, 1);
            }
        } else {
            result.symbol = as_symbol_find(parser->assembler, name, 1);
        }
        result.coefficient = 1;
        return result;
    }
    parser->failed = 1;
    return result;
}

static AsExpression parse_shift(ExpressionParser *parser) {
    AsExpression left = parse_primary(parser);
    for (;;) {
        skip_space(parser);
        if (parser->cursor[0] != '<' || parser->cursor[1] != '<') break;
        parser->cursor += 2;
        AsExpression right = parse_primary(parser);
        if (left.symbol >= 0 || right.symbol >= 0 || right.value < 0 || right.value > 63) parser->failed = 1;
        else left.value = (int64_t)((uint64_t)left.value << (unsigned)right.value);
    }
    return left;
}

static AsExpression combine(ExpressionParser *parser, AsExpression left, AsExpression right, int sign) {
    left.value += sign * right.value;
    if (right.symbol < 0) return left;
    int right_coefficient = sign * right.coefficient;
    if (left.symbol < 0) {
        left.symbol = right.symbol;
        left.coefficient = right_coefficient;
        return left;
    }
    if (left.symbol == right.symbol) {
        left.coefficient += right_coefficient;
        if (!left.coefficient) left.symbol = -1;
        return left;
    }
    AsSymbol *first = &parser->assembler->symbols[left.symbol];
    AsSymbol *second = &parser->assembler->symbols[right.symbol];
    if (first->defined && second->defined && first->section == second->section && left.coefficient == 1 && right_coefficient == -1) {
        left.value += (int64_t)first->value - (int64_t)second->value;
        left.symbol = -1;
        left.coefficient = 0;
        return left;
    }
    if (parser->assembler->pass == 1) {
        left.value = 0;
        left.symbol = -1;
        left.coefficient = 0;
        return left;
    }
    parser->failed = 1;
    return left;
}

static AsExpression parse_sum(ExpressionParser *parser) {
    AsExpression left = parse_shift(parser);
    for (;;) {
        skip_space(parser);
        if (*parser->cursor != '+' && *parser->cursor != '-') break;
        int sign = *parser->cursor == '+' ? 1 : -1;
        ++parser->cursor;
        AsExpression right = parse_shift(parser);
        left = combine(parser, left, right, sign);
    }
    return left;
}

int as_expression(Assembler *assembler, const char *text, int64_t *value, int *symbol, int64_t *addend) {
    ExpressionParser parser = {assembler, text, 0};
    AsExpression expression = parse_sum(&parser);
    skip_space(&parser);
    if (*parser.cursor || parser.failed) return -1;
    if (expression.symbol >= 0 && expression.coefficient != 1) {
        AsSymbol *entry = &assembler->symbols[expression.symbol];
        if (!entry->defined || expression.coefficient != -1) return -1;
        expression.value -= (int64_t)entry->value;
        expression.symbol = -1;
    }
    *symbol = expression.symbol;
    *value = expression.value;
    *addend = expression.value;
    return 0;
}
