#include "compiler.h"

char *new_label(void) {
    Buffer label = {0};
    buffer_text(&label, "__orion_cc_");
    buffer_number(&label, ++compiler.label_number);
    buffer_char(&label, 0);
    return label.data;
}

void emit_label(Buffer *buffer, const char *label) {
    buffer_text(buffer, label);
    buffer_text(buffer, ":\n");
}

void emit_jump(Buffer *buffer, const char *instruction, const char *label) {
    buffer_text(buffer, instruction);
    buffer_char(buffer, ' ');
    buffer_text(buffer, label);
    buffer_char(buffer, '\n');
}

void emit_local_memory(Buffer *buffer, const char *instruction, const char *left, size_t offset, const char *right) {
    (void)left;
    (void)right;
    buffer_text(buffer, instruction);
    buffer_text(buffer, " qword [rbp - ");
    buffer_number(buffer, offset);
    buffer_text(buffer, "], rax\n");
}

size_t allocate_temp(Function *function) {
    ++function->temp_depth;
    if (function->temp_depth > function->max_temp_depth) function->max_temp_depth = function->temp_depth;
    return align_value(function->local_bytes, 8) + function->temp_depth * 8;
}

void release_temp(Function *function) {
    if (function->temp_depth) --function->temp_depth;
}

void emit_memory_load(Buffer *buffer, CType type, const char *address_register) {
    size_t size = type.pointers ? 8 : type_size(type);
    if (size == 1) {
        buffer_text(buffer, "mov al, byte [");
        buffer_text(buffer, address_register);
        buffer_text(buffer, "]\n");
        if (type.is_unsigned || type.base == TY_BOOL) buffer_text(buffer, "and rax, 255\n");
        else buffer_text(buffer, "shl rax, 56\nsar rax, 56\n");
    } else if (size == 2) {
        buffer_text(buffer, "mov ax, word [");
        buffer_text(buffer, address_register);
        buffer_text(buffer, "]\n");
        if (type.is_unsigned) buffer_text(buffer, "and rax, 65535\n");
        else buffer_text(buffer, "shl rax, 48\nsar rax, 48\n");
    } else if (size == 4) {
        buffer_text(buffer, "mov eax, dword [");
        buffer_text(buffer, address_register);
        buffer_text(buffer, "]\n");
        if (!type.is_unsigned) buffer_text(buffer, "shl rax, 32\nsar rax, 32\n");
    } else {
        buffer_text(buffer, "mov rax, qword [");
        buffer_text(buffer, address_register);
        buffer_text(buffer, "]\n");
    }
}

void emit_memory_store(Buffer *buffer, CType type, const char *address_register, const char *value_register) {
    size_t size = type.pointers ? 8 : type_size(type);
    const char *size_name = size == 1 ? "byte" : size == 2 ? "word" : size == 4 ? "dword" : "qword";
    const char *value_name = size == 1 ? "dl" : size == 2 ? "dx" : size == 4 ? "edx" : "rdx";
    buffer_text(buffer, "mov ");
    buffer_text(buffer, size_name);
    buffer_text(buffer, " [");
    buffer_text(buffer, address_register);
    buffer_text(buffer, "], ");
    buffer_text(buffer, value_register ? value_register : value_name);
    buffer_char(buffer, '\n');
}

void emit_normalize_bool(Buffer *buffer) {
    char *truth = new_label();
    char *done = new_label();
    buffer_text(buffer, "cmp rax, 0\n");
    emit_jump(buffer, "jne", truth);
    buffer_text(buffer, "mov rax, 0\n");
    emit_jump(buffer, "jmp", done);
    emit_label(buffer, truth);
    buffer_text(buffer, "mov rax, 1\n");
    emit_label(buffer, done);
    free(truth);
    free(done);
}

void generate_address(Function *function, Node *node) {
    if (node->kind == N_IDENTIFIER) {
        Variable variable;
        if (!lookup_variable(node->text, &variable)) fail("unknown variable");
        if (variable.global) {
            buffer_text(&function->code, "lea rax, [rel ");
            buffer_text(&function->code, variable.name);
            buffer_text(&function->code, "]\n");
        } else {
            buffer_text(&function->code, "lea rax, [rbp - ");
            buffer_number(&function->code, variable.offset);
            buffer_text(&function->code, "]\n");
        }
        return;
    }
    if (node->kind == N_UNARY && node->op == '*') {
        generate_expression(function, node->left);
        return;
    }
    if (node->kind == N_INDEX) {
        generate_expression(function, node->left);
        size_t base_slot = allocate_temp(function);
        emit_local_memory(&function->code, "mov", "qword", base_slot, NULL);
        generate_expression(function, node->right);
        CType base_type = node_type(node->left);
        if (base_type.is_array) base_type.is_array = 0;
        if (!base_type.pointers) fail("subscripted value is not a pointer");
        --base_type.pointers;
        size_t scale = type_size(base_type);
        if (scale == 2) buffer_text(&function->code, "shl rax, 1\n");
        else if (scale == 4) buffer_text(&function->code, "shl rax, 2\n");
        else if (scale == 8) buffer_text(&function->code, "shl rax, 3\n");
        buffer_text(&function->code, "mov rcx, rax\n");
        buffer_text(&function->code, "mov rax, qword [rbp - ");
        buffer_number(&function->code, base_slot);
        buffer_text(&function->code, "]\n");
        buffer_text(&function->code, "add rax, rcx\n");
        release_temp(function);
        return;
    }
    fail("expression is not an assignable object");
}

void generate_load_from_address(Function *function, CType type) {
    buffer_text(&function->code, "mov rcx, rax\n");
    emit_memory_load(&function->code, type, "rcx");
}

