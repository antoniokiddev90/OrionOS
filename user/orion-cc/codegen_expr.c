#include "compiler.h"

void generate_call(Function *function, Node *node) {
    size_t old_depth = function->temp_depth;
    size_t *argument_slots = NULL;
    if (node->argument_count) {
        argument_slots = malloc(node->argument_count * sizeof(size_t));
        if (!argument_slots) fail("out of memory");
    }
    for (size_t i = 0; i < node->argument_count; ++i) {
        argument_slots[i] = allocate_temp(function);
        generate_expression(function, node->arguments[i]);
        emit_local_memory(&function->code, "mov", "qword", argument_slots[i], NULL);
    }
    static const char *registers[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    size_t register_count = node->argument_count < 6 ? node->argument_count : 6;
    for (size_t i = 0; i < register_count; ++i) {
        buffer_text(&function->code, "mov ");
        buffer_text(&function->code, registers[i]);
        buffer_text(&function->code, ", qword [rbp - ");
        buffer_number(&function->code, argument_slots[i]);
        buffer_text(&function->code, "]\n");
    }
    size_t stack_arguments = node->argument_count > 6 ? node->argument_count - 6 : 0;
    if (stack_arguments & 1) buffer_text(&function->code, "sub rsp, 8\n");
    for (size_t i = node->argument_count; i > 6; --i) {
        buffer_text(&function->code, "mov rax, qword [rbp - ");
        buffer_number(&function->code, argument_slots[i - 1]);
        buffer_text(&function->code, "]\npush rax\n");
    }
    buffer_text(&function->code, "call ");
    buffer_text(&function->code, node->left->text);
    buffer_char(&function->code, '\n');
    if (stack_arguments + (stack_arguments & 1)) {
        buffer_text(&function->code, "add rsp, ");
        buffer_number(&function->code, (stack_arguments + (stack_arguments & 1)) * 8);
        buffer_char(&function->code, '\n');
    }
    function->temp_depth = old_depth;
    free(argument_slots);
}

void generate_expression(Function *function, Node *node) {
    if (node->kind == N_NUMBER) {
        buffer_text(&function->code, "mov rax, ");
        buffer_signed(&function->code, node->value);
        buffer_char(&function->code, '\n');
        return;
    }
    if (node->kind == N_STRING) {
        char *label = new_label();
        buffer_text(&function->code, "lea rax, [rel ");
        buffer_text(&function->code, label);
        buffer_text(&function->code, "]\n");
        buffer_text(&compiler.rodata, "align 1\n");
        emit_label(&compiler.rodata, label);
        buffer_text(&compiler.rodata, "db ");
        if (!node->length) buffer_char(&compiler.rodata, '0');
        for (size_t i = 0; i < node->length; ++i) {
            if (i) buffer_char(&compiler.rodata, ',');
            buffer_number(&compiler.rodata, (unsigned char)node->text[i]);
        }
        if (node->length) buffer_text(&compiler.rodata, ",0");
        buffer_char(&compiler.rodata, '\n');
        free(label);
        return;
    }
    if (node->kind == N_IDENTIFIER) {
        Variable variable;
        if (!lookup_variable(node->text, &variable)) fail("unknown variable");
        if (variable.type.is_array) {
            generate_address(function, node);
            return;
        }
        generate_address(function, node);
        generate_load_from_address(function, variable.type);
        return;
    }
    if (node->kind == N_INDEX) {
        generate_address(function, node);
        generate_load_from_address(function, node_type(node));
        return;
    }
    if (node->kind == N_CALL) {
        generate_call(function, node);
        return;
    }
    if (node->kind == N_ASSIGN) {
        generate_assignment(function, node);
        return;
    }
    if (node->kind == N_BINARY) {
        generate_binary(function, node);
        return;
    }
    if (node->kind == N_CONDITIONAL) {
        char *otherwise = new_label();
        char *done = new_label();
        generate_expression(function, node->left);
        buffer_text(&function->code, "cmp rax, 0\n");
        emit_jump(&function->code, "je", otherwise);
        generate_expression(function, node->right);
        emit_jump(&function->code, "jmp", done);
        emit_label(&function->code, otherwise);
        generate_expression(function, node->third);
        emit_label(&function->code, done);
        free(otherwise);
        free(done);
        return;
    }
    if (node->kind == N_CAST) {
        generate_expression(function, node->left);
        if (node->type.base == TY_BOOL && !node->type.pointers) emit_normalize_bool(&function->code);
        return;
    }
    if (node->kind == N_PREINC || node->kind == N_POSTINC) {
        generate_update(function, node, 1, node->kind == N_POSTINC);
        return;
    }
    if (node->kind == N_PREDEC || node->kind == N_POSTDEC) {
        generate_update(function, node, 0, node->kind == N_POSTDEC);
        return;
    }
    if (node->kind == N_UNARY) {
        if (node->op == '&') {
            generate_address(function, node->left);
            return;
        }
        if (node->op == '*') {
            generate_expression(function, node->left);
            generate_load_from_address(function, node_type(node));
            return;
        }
        generate_expression(function, node->left);
        if (node->op == '-') buffer_text(&function->code, "neg rax\n");
        else if (node->op == '~') buffer_text(&function->code, "not rax\n");
        else if (node->op == '!') {
            buffer_text(&function->code, "cmp rax, 0\n");
            char *truth = new_label();
            char *done = new_label();
            emit_jump(&function->code, "je", truth);
            buffer_text(&function->code, "mov rax, 0\n");
            emit_jump(&function->code, "jmp", done);
            emit_label(&function->code, truth);
            buffer_text(&function->code, "mov rax, 1\n");
            emit_label(&function->code, done);
            free(truth);
            free(done);
        }
        return;
    }
    fail("unsupported expression");
}

