#include "compiler.h"

int peek_word(const char *word) {
    return peek()->kind == T_IDENTIFIER && strcmp(peek()->text, word) == 0;
}

void parse_storage(int *is_typedef, int *is_static, int *is_extern) {
    for (;;) {
        if (accept_word("typedef")) { *is_typedef = 1; continue; }
        if (accept_word("static")) { *is_static = 1; continue; }
        if (accept_word("extern")) { *is_extern = 1; continue; }
        break;
    }
}

Initializer parse_initializer(void) {
    Initializer initializer;
    memset(&initializer, 0, sizeof(initializer));
    if (peek()->kind == T_STRING) {
        initializer.string_value = parse_primary();
        return initializer;
    }
    if (accept('{')) {
        if (!accept('}')) {
            size_t next_index = 0;
            for (;;) {
                size_t index = next_index;
                if (accept('[')) {
                    long long designated_index = constant_value(parse_expression(1));
                    if (designated_index < 0 || designated_index > 0x100000) fail("array designator is out of range");
                    expect(']', "closing bracket expected after array designator");
                    expect('=', "equals sign expected after array designator");
                    index = (size_t)designated_index;
                    next_index = index + 1;
                } else ++next_index;
                if (index >= initializer.capacity) {
                    size_t old_capacity = initializer.capacity;
                    initializer.items = array_grow(initializer.items, &initializer.capacity, index + 1, sizeof(Node *));
                    memset(initializer.items + old_capacity, 0, (initializer.capacity - old_capacity) * sizeof(Node *));
                }
                initializer.items[index] = parse_expression(2);
                if (initializer.count < index + 1) initializer.count = index + 1;
                if (accept('}')) break;
                expect(',', "comma expected in initializer");
                if (accept('}')) break;
            }
        }
        return initializer;
    }
    initializer.items = array_grow(initializer.items, &initializer.capacity, 1, sizeof(Node *));
    initializer.items[initializer.count++] = parse_expression(2);
    return initializer;
}

void emit_string_data(Node *string_node, char **result_label) {
    char *label = new_label();
    buffer_text(&compiler.rodata, "align 1\n");
    emit_label(&compiler.rodata, label);
    buffer_text(&compiler.rodata, "db ");
    if (!string_node->length) buffer_char(&compiler.rodata, '0');
    for (size_t i = 0; i < string_node->length; ++i) {
        if (i) buffer_char(&compiler.rodata, ',');
        buffer_number(&compiler.rodata, (unsigned char)string_node->text[i]);
    }
    if (string_node->length) buffer_text(&compiler.rodata, ",0");
    buffer_char(&compiler.rodata, '\n');
    *result_label = label;
}

long long constant_value(Node *node) {
    if (node->kind == N_NUMBER) return node->value;
    if (node->kind == N_UNARY) {
        long long value = constant_value(node->left);
        if (node->op == '-') return -value;
        if (node->op == '+') return value;
        if (node->op == '!') return !value;
        if (node->op == '~') return ~value;
    }
    if (node->kind == N_BINARY) {
        long long left = constant_value(node->left);
        long long right = constant_value(node->right);
        if (node->op == '+') return left + right;
        if (node->op == '-') return left - right;
        if (node->op == '*') return left * right;
        if (node->op == '/' && right) return left / right;
        if (node->op == '%' && right) return left % right;
        if (node->op == '&') return left & right;
        if (node->op == '|') return left | right;
        if (node->op == '^') return left ^ right;
        if (node->op == OP_SHL) return left << right;
        if (node->op == OP_SHR) return left >> right;
    }
    fail("global initializer must be a constant expression");
    return 0;
}

void emit_global_initializer(const char *name, CType type, Initializer *initializer, int is_static) {
    Variable *variable = add_global(name, type, 1);
    size_t element_size = type_size((CType){type.base, type.pointers, type.is_unsigned, 0, 0});
    if (type.is_array) {
        if (!type.count && initializer->string_value) type.count = initializer->string_value->length + 1;
        if (!type.count && initializer->count) type.count = initializer->count;
        if (!type.count) fail("array size cannot be inferred");
        variable->type = type;
        size_t total = type.count * element_size;
        Buffer *target = initializer->count || initializer->string_value ? &compiler.data : &compiler.bss;
        buffer_text(&compiler.text, is_static ? "" : "global ");
        if (!is_static) {
            buffer_text(&compiler.text, name);
            buffer_char(&compiler.text, '\n');
        }
        buffer_text(target, "align 8\n");
        emit_label(target, name);
        if (!initializer->count && !initializer->string_value) {
            buffer_text(target, "resb ");
            buffer_number(target, total);
            buffer_char(target, '\n');
            return;
        }
        size_t supplied = initializer->string_value ? initializer->string_value->length + 1 : initializer->count;
        for (size_t i = 0; i < type.count; ++i) {
            long long value = 0;
            if (initializer->string_value && i < initializer->string_value->length) value = (unsigned char)initializer->string_value->text[i];
            else if (initializer->count && i < initializer->count && initializer->items[i]) value = constant_value(initializer->items[i]);
            const char *directive = element_size == 1 ? "db " : element_size == 2 ? "dw " : element_size == 4 ? "dd " : "dq ";
            buffer_text(target, directive);
            buffer_signed(target, value);
            buffer_char(target, '\n');
        }
        (void)supplied;
        return;
    }
    if (initializer->string_value) {
        if (!type.pointers) fail("string initializer requires a pointer type");
        char *label;
        emit_string_data(initializer->string_value, &label);
        buffer_text(&compiler.text, is_static ? "" : "global ");
        if (!is_static) {
            buffer_text(&compiler.text, name);
            buffer_char(&compiler.text, '\n');
        }
        buffer_text(&compiler.data, "align 8\n");
        emit_label(&compiler.data, name);
        buffer_text(&compiler.data, "dq ");
        buffer_text(&compiler.data, label);
        buffer_char(&compiler.data, '\n');
        free(label);
        return;
    }
    if (initializer->count > 1 || (initializer->count && !initializer->items[0])) fail("invalid scalar initializer");
    long long value = initializer->count ? constant_value(initializer->items[0]) : 0;
    Buffer *target = initializer->count ? &compiler.data : &compiler.bss;
    buffer_text(&compiler.text, is_static ? "" : "global ");
    if (!is_static) {
        buffer_text(&compiler.text, name);
        buffer_char(&compiler.text, '\n');
    }
    buffer_text(target, "align 8\n");
    emit_label(target, name);
    if (!initializer->count) {
        buffer_text(target, "resb ");
        buffer_number(target, element_size);
        buffer_char(target, '\n');
    } else {
        const char *directive = element_size == 1 ? "db " : element_size == 2 ? "dw " : element_size == 4 ? "dd " : "dq ";
        buffer_text(target, directive);
        buffer_signed(target, value);
        buffer_char(target, '\n');
    }
}

void add_function_symbol(const char *name, CType type, int defined) {
    Variable *existing = NULL;
    for (size_t i = 0; i < compiler.global_count; ++i) if (strcmp(compiler.globals[i].name, name) == 0) existing = &compiler.globals[i];
    if (existing) {
        if (existing->global != 2) fail("symbol is already declared as a variable");
        if (defined && existing->defined) fail("duplicate function definition");
        existing->type = type;
        if (defined) existing->defined = 1;
        return;
    }
    compiler.globals = array_grow(compiler.globals, &compiler.global_capacity, compiler.global_count + 1, sizeof(Variable));
    Variable *function = &compiler.globals[compiler.global_count++];
    memset(function, 0, sizeof(*function));
    function->name = copy_text(name);
    function->type = type;
    function->global = 2;
    function->defined = defined;
}

void emit_parameter_store(Buffer *output, size_t parameter_index, Variable *variable) {
    static const char *registers[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    if (parameter_index < 6) {
        buffer_text(output, "mov rax, ");
        buffer_text(output, registers[parameter_index]);
        buffer_char(output, '\n');
    } else {
        buffer_text(output, "mov rax, qword [rbp + ");
        buffer_number(output, 16 + (parameter_index - 6) * 8);
        buffer_text(output, "]\n");
    }
    buffer_text(output, "mov ");
    size_t size = variable->type.pointers ? 8 : type_size(variable->type);
    buffer_text(output, size == 1 ? "byte [rbp - " : size == 2 ? "word [rbp - " : size == 4 ? "dword [rbp - " : "qword [rbp - ");
    buffer_number(output, variable->offset);
    buffer_text(output, size == 1 ? "], al\n" : size == 2 ? "], ax\n" : size == 4 ? "], eax\n" : "], rax\n");
}

void finish_function(Function *function, Parameter *parameters, size_t parameter_count) {
    Buffer *output = &compiler.text;
    buffer_text(output, "section .text\n");
    if (!function->is_static) {
        buffer_text(output, "global ");
        buffer_text(output, function->name);
        buffer_char(output, '\n');
    }
    emit_label(output, function->name);
    buffer_text(output, "push rbp\nmov rbp, rsp\n");
    size_t frame_size = align_value(align_value(function->local_bytes, 8) + function->max_temp_depth * 8, 16);
    if (frame_size) {
        buffer_text(output, "sub rsp, ");
        buffer_number(output, frame_size);
        buffer_char(output, '\n');
    }
    for (size_t i = 0; i < parameter_count; ++i) emit_parameter_store(output, i, &function->locals[i]);
    buffer_bytes(output, function->code.data, function->code.size);
    buffer_text(output, "mov rax, 0\n");
    emit_label(output, function->exit_label);
    buffer_text(output, "leave\nret\n");
    free(parameters);
}

void emit_local_array_initializer(Function *function, Variable *variable, Initializer *initializer) {
    size_t count = variable->type.count;
    CType element = {variable->type.base, variable->type.pointers, variable->type.is_unsigned, 0, 0};
    size_t width = type_size(element);
    for (size_t i = 0; i < count; ++i) {
        Node *value = NULL;
        if (initializer->string_value) {
            value = number_node(i < initializer->string_value->length ? (unsigned char)initializer->string_value->text[i] : 0);
        } else if (i < initializer->count && initializer->items[i]) value = initializer->items[i];
        else value = number_node(0);
        generate_expression(function, value);
        size_t value_slot = allocate_temp(function);
        emit_local_memory(&function->code, "mov", "qword", value_slot, NULL);
        buffer_text(&function->code, "lea rcx, [rbp - ");
        buffer_number(&function->code, variable->offset);
        buffer_text(&function->code, "]\n");
        if (i != 0) {
            buffer_text(&function->code, "add rcx, ");
            buffer_number(&function->code, i * width);
            buffer_char(&function->code, '\n');
        }
        buffer_text(&function->code, "mov rdx, qword [rbp - ");
        buffer_number(&function->code, value_slot);
        buffer_text(&function->code, "]\n");
        emit_memory_store(&function->code, element, "rcx", NULL);
        release_temp(function);
    }
}

void parse_local_declaration(void) {
    Function *function = active_function;
    int is_typedef = 0;
    int is_static = 0;
    int is_extern = 0;
    parse_storage(&is_typedef, &is_static, &is_extern);
    CType base = parse_type_specifier();
    if (accept(';')) return;
    for (;;) {
        CType type = parse_pointer_type(base);
        if (peek()->kind != T_IDENTIFIER) fail("local variable name expected");
        char *name = expect_identifier();
        if (accept('[')) {
            type.is_array = 1;
            type.count = 0;
            if (peek()->kind == T_NUMBER) {
                long long count = peek()->value;
                ++compiler.position;
                if (count < 0 || count > 0x100000) fail("array size is invalid");
                type.count = (size_t)count;
            }
            expect(']', "closing bracket expected after array size");
        }
        if (is_typedef) {
            add_alias(name, type);
        } else {
            Initializer initializer;
            memset(&initializer, 0, sizeof(initializer));
            int has_initializer = accept('=');
            if (has_initializer) initializer = parse_initializer();
            if (type.is_array && !type.count) {
                if (initializer.string_value) type.count = initializer.string_value->length + 1;
                else if (initializer.count) type.count = initializer.count;
                else fail("array size cannot be inferred");
            }
            if (type.is_array && initializer.count > type.count) fail("too many array initializers");
            Variable *variable = add_local(function, name, type);
            if (has_initializer && type.is_array) emit_local_array_initializer(function, variable, &initializer);
            else if (has_initializer) {
                if (initializer.count > 1 || (initializer.count && !initializer.items[0])) fail("invalid scalar initializer");
                Node *right = initializer.string_value;
                if (!initializer.count && !right) fail("initializer is missing");
                if (initializer.count) right = initializer.items[0];
                if (right->kind == N_STRING && !type.pointers) fail("string initializer requires a pointer type");
                Node *left = variable_node(name);
                Node *assignment = new_node(N_ASSIGN, '=', left, right);
                generate_assignment(function, assignment);
            }
            free(initializer.items);
            free(initializer.string_value ? initializer.string_value->text : NULL);
            free(initializer.string_value);
        }
        free(name);
        if (!accept(',')) break;
    }
    expect(';', "semicolon expected after declaration");
    (void)is_static;
    (void)is_extern;
}
