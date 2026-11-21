#include "compiler.h"

int type_keyword(const char *name) {
    return strcmp(name, "void") == 0 || strcmp(name, "bool") == 0 || strcmp(name, "_Bool") == 0 || strcmp(name, "char") == 0 || strcmp(name, "short") == 0 || strcmp(name, "int") == 0 || strcmp(name, "long") == 0 || strcmp(name, "signed") == 0 || strcmp(name, "unsigned") == 0 || strcmp(name, "const") == 0 || strcmp(name, "volatile") == 0 || strcmp(name, "restrict") == 0 || strcmp(name, "register") == 0 || strcmp(name, "auto") == 0 || strcmp(name, "static") == 0 || strcmp(name, "extern") == 0 || strcmp(name, "typedef") == 0 || strcmp(name, "inline") == 0 || strcmp(name, "enum") == 0;
}

Alias *find_alias(const char *name) {
    for (size_t i = 0; i < compiler.alias_count; ++i) if (strcmp(compiler.aliases[i].name, name) == 0) return &compiler.aliases[i];
    return NULL;
}

int is_type_start(void) {
    if (peek()->kind != T_IDENTIFIER) return 0;
    return type_keyword(peek()->text) || find_alias(peek()->text) != NULL;
}

size_t type_size(CType type) {
    if (type.pointers || type.is_array) return 8;
    if (type.base == TY_CHAR || type.base == TY_BOOL) return 1;
    if (type.base == TY_SHORT) return 2;
    if (type.base == TY_INT) return 4;
    if (type.base == TY_LONG) return 8;
    if (type.base == TY_VOID) return 1;
    return 8;
}

CType parse_type_specifier(void) {
    CType type = {TY_INT, 0, 0, 0, 0};
    int found = 0;
    int saw_unsigned = 0;
    int saw_signed = 0;
    int saw_long = 0;
    int saw_short = 0;
    while (peek()->kind == T_IDENTIFIER) {
        const char *name = peek()->text;
        if (strcmp(name, "enum") == 0) {
            ++compiler.position;
            if (peek()->kind == T_IDENTIFIER) ++compiler.position;
            type.base = TY_INT;
            found = 1;
            if (accept('{')) {
                long long next_value = 0;
                if (!accept('}')) {
                    for (;;) {
                        char *enumerator = expect_identifier();
                        if (accept('=')) next_value = constant_value(parse_expression(2));
                        add_enum_constant(enumerator, next_value);
                        free(enumerator);
                        if (next_value == 0x7fffffffffffffffLL) fail("enumerator value is out of range");
                        ++next_value;
                        if (accept('}')) break;
                        expect(',', "comma expected between enumerators");
                        if (accept('}')) break;
                    }
                }
            }
            break;
        }
        if (strcmp(name, "const") == 0 || strcmp(name, "volatile") == 0 || strcmp(name, "restrict") == 0 || strcmp(name, "register") == 0 || strcmp(name, "auto") == 0 || strcmp(name, "static") == 0 || strcmp(name, "extern") == 0 || strcmp(name, "typedef") == 0 || strcmp(name, "inline") == 0) {
            ++compiler.position;
            continue;
        }
        if (strcmp(name, "unsigned") == 0) { saw_unsigned = 1; found = 1; ++compiler.position; continue; }
        if (strcmp(name, "signed") == 0) { saw_signed = 1; found = 1; ++compiler.position; continue; }
        if (strcmp(name, "long") == 0) { saw_long = 1; found = 1; ++compiler.position; type.base = TY_LONG; continue; }
        if (strcmp(name, "short") == 0) { saw_short = 1; found = 1; ++compiler.position; type.base = TY_SHORT; continue; }
        if (strcmp(name, "int") == 0) { found = 1; ++compiler.position; if (!saw_long && !saw_short) type.base = TY_INT; continue; }
        if (strcmp(name, "char") == 0) { found = 1; type.base = TY_CHAR; ++compiler.position; continue; }
        if (strcmp(name, "void") == 0) { found = 1; type.base = TY_VOID; ++compiler.position; continue; }
        if (strcmp(name, "bool") == 0 || strcmp(name, "_Bool") == 0) { found = 1; type.base = TY_BOOL; ++compiler.position; continue; }
        Alias *alias = find_alias(name);
        if (alias) {
            if (found) break;
            type = alias->type;
            found = 1;
            ++compiler.position;
            continue;
        }
        break;
    }
    if (!found) fail("type specifier expected");
    if (saw_short) type.base = TY_SHORT;
    else if (saw_long) type.base = TY_LONG;
    if (saw_unsigned) type.is_unsigned = 1;
    if (saw_signed) type.is_unsigned = 0;
    return type;
}

void add_enum_constant(const char *name, long long value) {
    for (size_t i = 0; i < compiler.global_count; ++i) {
        if (strcmp(compiler.globals[i].name, name) == 0) fail("duplicate enumerator");
    }
    if (find_alias(name)) fail("enumerator conflicts with a typedef name");
    if (active_function) {
        for (size_t i = 0; i < active_function->local_count; ++i) {
            if (strcmp(active_function->locals[i].name, name) == 0) fail("enumerator conflicts with a local name");
        }
    }
    compiler.globals = array_grow(compiler.globals, &compiler.global_capacity, compiler.global_count + 1, sizeof(Variable));
    Variable *enumerator = &compiler.globals[compiler.global_count++];
    memset(enumerator, 0, sizeof(*enumerator));
    enumerator->name = copy_text(name);
    enumerator->type = (CType){TY_INT, 0, 0, 0, 0};
    enumerator->global = 3;
    enumerator->defined = 1;
    enumerator->constant_value = value;
}

int find_enum_constant(const char *name, long long *value) {
    if (active_function) {
        for (size_t i = active_function->local_count; i; --i) {
            if (strcmp(active_function->locals[i - 1].name, name) == 0) return 0;
        }
    }
    for (size_t i = compiler.global_count; i; --i) {
        Variable *symbol = &compiler.globals[i - 1];
        if (symbol->global == 3 && strcmp(symbol->name, name) == 0) {
            if (value) *value = symbol->constant_value;
            return 1;
        }
    }
    return 0;
}

CType parse_pointer_type(CType type) {
    while (accept('*')) {
        ++type.pointers;
        type.is_array = 0;
        type.count = 0;
        while (accept_word("const") || accept_word("volatile") || accept_word("restrict")) { }
    }
    return type;
}

void add_alias(const char *name, CType type) {
    Alias *existing = find_alias(name);
    if (existing) {
        existing->type = type;
        return;
    }
    compiler.aliases = array_grow(compiler.aliases, &compiler.alias_capacity, compiler.alias_count + 1, sizeof(Alias));
    compiler.aliases[compiler.alias_count].name = copy_text(name);
    compiler.aliases[compiler.alias_count].type = type;
    ++compiler.alias_count;
}

void initialize_builtin_aliases(void) {
    CType type = {TY_LONG, 0, 1, 0, 0};
    add_alias("size_t", type);
    type.is_unsigned = 0;
    add_alias("ssize_t", type);
    type.base = TY_LONG;
    add_alias("ptrdiff_t", type);
    type.base = TY_INT;
    type.is_unsigned = 1;
    add_alias("uint8_t", (CType){TY_CHAR, 0, 1, 0, 0});
    add_alias("uint16_t", (CType){TY_SHORT, 0, 1, 0, 0});
    add_alias("uint32_t", (CType){TY_INT, 0, 1, 0, 0});
    add_alias("uint64_t", (CType){TY_LONG, 0, 1, 0, 0});
    add_alias("int8_t", (CType){TY_CHAR, 0, 0, 0, 0});
    add_alias("int16_t", (CType){TY_SHORT, 0, 0, 0, 0});
    add_alias("int32_t", (CType){TY_INT, 0, 0, 0, 0});
    add_alias("int64_t", (CType){TY_LONG, 0, 0, 0, 0});
}

int lookup_variable(const char *name, Variable *result) {
    if (active_function) {
        for (size_t i = active_function->local_count; i; --i) {
            Variable *variable = &active_function->locals[i - 1];
            if (strcmp(variable->name, name) == 0) {
                if (result) *result = *variable;
                return 1;
            }
        }
    }
    for (size_t i = compiler.global_count; i; --i) {
        Variable *variable = &compiler.globals[i - 1];
        if (strcmp(variable->name, name) == 0) {
            if (result) *result = *variable;
            return 1;
        }
    }
    return 0;
}

CType node_type(Node *node) {
    if (!node) return (CType){TY_INT, 0, 0, 0, 0};
    if (node->kind == N_NUMBER) return node->type;
    if (node->kind == N_STRING) return (CType){TY_CHAR, 1, 0, 0, 0};
    if (node->kind == N_IDENTIFIER) {
        Variable variable;
        if (lookup_variable(node->text, &variable)) {
            CType type = variable.type;
            if (type.is_array) {
                type.is_array = 0;
                type.pointers++;
            }
            return type;
        }
        return (CType){TY_INT, 0, 0, 0, 0};
    }
    if (node->kind == N_INDEX) {
        CType type = node_type(node->left);
        if (type.is_array) type.is_array = 0;
        if (type.pointers) --type.pointers;
        type.count = 0;
        return type;
    }
    if (node->kind == N_CAST) return node->type;
    if (node->kind == N_CALL) return node->type;
    if (node->kind == N_ASSIGN || node->kind == N_BINARY || node->kind == N_CONDITIONAL) return node_type(node->left);
    if (node->kind == N_UNARY) {
        CType type = node_type(node->left);
        if (node->op == '&') ++type.pointers;
        if (node->op == '*') {
            if (type.pointers) --type.pointers;
            type.is_array = 0;
        }
        if (node->op == '!') return (CType){TY_BOOL, 0, 0, 0, 0};
        return type;
    }
    if (node->kind == N_PREINC || node->kind == N_PREDEC || node->kind == N_POSTINC || node->kind == N_POSTDEC) return node_type(node->left);
    return (CType){TY_INT, 0, 0, 0, 0};
}

Variable *add_local(Function *function, const char *name, CType type) {
    if (function->scope_depth == 0) fail("local scope is invalid");
    for (size_t i = function->local_count; i; --i) {
        Variable *previous_local = &function->locals[i - 1];
        if (previous_local->scope_depth == function->scope_depth && strcmp(previous_local->name, name) == 0) fail("duplicate local variable");
    }
    size_t bytes = type.is_array ? type.count * type_size((CType){type.base, type.pointers, type.is_unsigned, 0, 0}) : type_size(type);
    if (!bytes || bytes > 0x100000) fail("local object is too large");
    size_t alignment = type.is_array ? type_size((CType){type.base, type.pointers, type.is_unsigned, 0, 0}) : type_size(type);
    if (alignment > 8) alignment = 8;
    function->local_bytes = align_value(function->local_bytes, alignment);
    function->local_bytes += bytes;
    function->locals = array_grow(function->locals, &function->local_capacity, function->local_count + 1, sizeof(Variable));
    Variable *variable = &function->locals[function->local_count++];
    memset(variable, 0, sizeof(*variable));
    variable->name = copy_text(name);
    variable->type = type;
    variable->offset = function->local_bytes;
    variable->scope_depth = function->scope_depth;
    return variable;
}

Variable *add_global(const char *name, CType type, int defined) {
    for (size_t i = 0; i < compiler.global_count; ++i) {
        if (strcmp(compiler.globals[i].name, name) == 0) {
            if (defined && compiler.globals[i].defined) fail("duplicate global variable");
            compiler.globals[i].type = type;
            if (defined) compiler.globals[i].defined = 1;
            return &compiler.globals[i];
        }
    }
    compiler.globals = array_grow(compiler.globals, &compiler.global_capacity, compiler.global_count + 1, sizeof(Variable));
    Variable *variable = &compiler.globals[compiler.global_count++];
    memset(variable, 0, sizeof(*variable));
    variable->name = copy_text(name);
    variable->type = type;
    variable->global = 1;
    variable->defined = defined;
    return variable;
}
