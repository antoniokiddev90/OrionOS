#include "compiler.h"

void generate_compare(Function *function, Node *node) {
    CType type = node_type(node->left);
    char *truth = new_label();
    char *done = new_label();
    buffer_text(&function->code, "cmp rax, rcx\n");
    const char *branch = NULL;
    if (node->op == OP_EQ) branch = "je";
    else if (node->op == OP_NE) branch = "jne";
    else if (node->op == '<') branch = type.is_unsigned ? "jb" : "jl";
    else if (node->op == '>') branch = type.is_unsigned ? "ja" : "jg";
    else if (node->op == OP_LE) branch = type.is_unsigned ? "jbe" : "jle";
    else if (node->op == OP_GE) branch = type.is_unsigned ? "jae" : "jge";
    emit_jump(&function->code, branch, truth);
    buffer_text(&function->code, "mov rax, 0\n");
    emit_jump(&function->code, "jmp", done);
    emit_label(&function->code, truth);
    buffer_text(&function->code, "mov rax, 1\n");
    emit_label(&function->code, done);
    free(truth);
    free(done);
}

void generate_logical(Function *function, Node *node) {
    char *short_label = new_label();
    char *done = new_label();
    generate_expression(function, node->left);
    buffer_text(&function->code, "cmp rax, 0\n");
    emit_jump(&function->code, node->op == OP_ANDAND ? "je" : "jne", short_label);
    generate_expression(function, node->right);
    buffer_text(&function->code, "cmp rax, 0\n");
    if (node->op == OP_ANDAND) {
        char *truth = new_label();
        emit_jump(&function->code, "jne", truth);
        buffer_text(&function->code, "mov rax, 0\n");
        emit_jump(&function->code, "jmp", done);
        emit_label(&function->code, truth);
        buffer_text(&function->code, "mov rax, 1\n");
        emit_jump(&function->code, "jmp", done);
        emit_label(&function->code, short_label);
        buffer_text(&function->code, "mov rax, 0\n");
        emit_label(&function->code, done);
        free(truth);
    } else {
        char *false_label = new_label();
        emit_jump(&function->code, "je", false_label);
        buffer_text(&function->code, "mov rax, 1\n");
        emit_jump(&function->code, "jmp", done);
        emit_label(&function->code, false_label);
        buffer_text(&function->code, "mov rax, 0\n");
        emit_jump(&function->code, "jmp", done);
        emit_label(&function->code, short_label);
        buffer_text(&function->code, "mov rax, 1\n");
        emit_label(&function->code, done);
        free(false_label);
    }
    free(short_label);
    free(done);
}

void generate_assignment(Function *function, Node *node) {
    CType type = node_type(node->left);
    generate_address(function, node->left);
    size_t address_slot = allocate_temp(function);
    emit_local_memory(&function->code, "mov", "qword", address_slot, NULL);
    if (node->op != '=') {
        buffer_text(&function->code, "mov rax, qword [rbp - ");
        buffer_number(&function->code, address_slot);
        buffer_text(&function->code, "]\n");
        generate_load_from_address(function, type);
        size_t old_value_slot = allocate_temp(function);
        emit_local_memory(&function->code, "mov", "qword", old_value_slot, NULL);
        generate_expression(function, node->right);
        buffer_text(&function->code, "mov rcx, rax\nmov rax, qword [rbp - ");
        buffer_number(&function->code, old_value_slot);
        buffer_text(&function->code, "]\n");
        int op = node->op == OP_ADD_ASSIGN ? '+' : node->op == OP_SUB_ASSIGN ? '-' : node->op == OP_MUL_ASSIGN ? '*' : node->op == OP_DIV_ASSIGN ? '/' : node->op == OP_MOD_ASSIGN ? '%' : node->op == OP_AND_ASSIGN ? '&' : node->op == OP_OR_ASSIGN ? '|' : node->op == OP_XOR_ASSIGN ? '^' : node->op == OP_SHL_ASSIGN ? OP_SHL : OP_SHR;
        if (op == '+') buffer_text(&function->code, "add rax, rcx\n");
        else if (op == '-') buffer_text(&function->code, "sub rax, rcx\n");
        else if (op == '*') buffer_text(&function->code, "imul rcx\n");
        else if (op == '&') buffer_text(&function->code, "and rax, rcx\n");
        else if (op == '|') buffer_text(&function->code, "or rax, rcx\n");
        else if (op == '^') buffer_text(&function->code, "xor rax, rcx\n");
        else if (op == OP_SHL) buffer_text(&function->code, "shl rax, cl\n");
        else if (op == OP_SHR) buffer_text(&function->code, type.is_unsigned ? "shr rax, cl\n" : "sar rax, cl\n");
        else if (op == '/' || op == '%') {
            if (type.is_unsigned) buffer_text(&function->code, "xor rdx, rdx\ndiv rcx\n");
            else buffer_text(&function->code, "mov rdx, rax\nsar rdx, 63\nidiv rcx\n");
            if (op == '%') buffer_text(&function->code, "mov rax, rdx\n");
        }
        release_temp(function);
    } else generate_expression(function, node->right);
    if (type.base == TY_BOOL && !type.pointers) emit_normalize_bool(&function->code);
    buffer_text(&function->code, "mov rdx, rax\nmov rcx, qword [rbp - ");
    buffer_number(&function->code, address_slot);
    buffer_text(&function->code, "]\n");
    emit_memory_store(&function->code, type, "rcx", NULL);
    release_temp(function);
}

void generate_binary(Function *function, Node *node) {
    if (node->op == ',') {
        generate_expression(function, node->left);
        generate_expression(function, node->right);
        return;
    }
    if (node->op == OP_ANDAND || node->op == OP_OROR) {
        generate_logical(function, node);
        return;
    }
    generate_expression(function, node->left);
    size_t left_slot = allocate_temp(function);
    emit_local_memory(&function->code, "mov", "qword", left_slot, NULL);
    generate_expression(function, node->right);
    buffer_text(&function->code, "mov rcx, rax\nmov rax, qword [rbp - ");
    buffer_number(&function->code, left_slot);
    buffer_text(&function->code, "]\n");
    release_temp(function);
    if (node->op == OP_EQ || node->op == OP_NE || node->op == '<' || node->op == '>' || node->op == OP_LE || node->op == OP_GE) {
        generate_compare(function, node);
        return;
    }
    if (node->op == '+' || node->op == '-') {
        CType left_type = node_type(node->left);
        CType right_type = node_type(node->right);
        if (left_type.pointers && !right_type.pointers) {
            size_t scale = type_size((CType){left_type.base, left_type.pointers - 1, left_type.is_unsigned, 0, 0});
            if (scale == 2) buffer_text(&function->code, "shl rcx, 1\n");
            else if (scale == 4) buffer_text(&function->code, "shl rcx, 2\n");
            else if (scale == 8) buffer_text(&function->code, "shl rcx, 3\n");
        } else if (node->op == '+' && right_type.pointers && !left_type.pointers) {
            size_t scale = type_size((CType){right_type.base, right_type.pointers - 1, right_type.is_unsigned, 0, 0});
            if (scale == 2) buffer_text(&function->code, "shl rax, 1\n");
            else if (scale == 4) buffer_text(&function->code, "shl rax, 2\n");
            else if (scale == 8) buffer_text(&function->code, "shl rax, 3\n");
            buffer_text(&function->code, "add rax, rcx\n");
            return;
        }
    }
    if (node->op == '+') buffer_text(&function->code, "add rax, rcx\n");
    else if (node->op == '-') buffer_text(&function->code, "sub rax, rcx\n");
    else if (node->op == '*') buffer_text(&function->code, "imul rcx\n");
    else if (node->op == '&') buffer_text(&function->code, "and rax, rcx\n");
    else if (node->op == '|') buffer_text(&function->code, "or rax, rcx\n");
    else if (node->op == '^') buffer_text(&function->code, "xor rax, rcx\n");
    else if (node->op == OP_SHL) buffer_text(&function->code, "shl rax, cl\n");
    else if (node->op == OP_SHR) buffer_text(&function->code, node_type(node->left).is_unsigned ? "shr rax, cl\n" : "sar rax, cl\n");
    else if (node->op == '/' || node->op == '%') {
        if (node_type(node->left).is_unsigned) buffer_text(&function->code, "xor rdx, rdx\ndiv rcx\n");
        else buffer_text(&function->code, "mov rdx, rax\nsar rdx, 63\nidiv rcx\n");
        if (node->op == '%') buffer_text(&function->code, "mov rax, rdx\n");
    } else fail("unsupported binary operator");
}

void generate_update(Function *function, Node *node, int increment, int postfix) {
    generate_address(function, node->left);
    size_t address_slot = allocate_temp(function);
    emit_local_memory(&function->code, "mov", "qword", address_slot, NULL);
    buffer_text(&function->code, "mov rax, qword [rbp - ");
    buffer_number(&function->code, address_slot);
    buffer_text(&function->code, "]\n");
    generate_load_from_address(function, node_type(node->left));
    size_t old_slot = 0;
    if (postfix) {
        old_slot = allocate_temp(function);
        emit_local_memory(&function->code, "mov", "qword", old_slot, NULL);
    }
    CType type = node_type(node->left);
    size_t amount = type.pointers ? type_size((CType){type.base, type.pointers - 1, type.is_unsigned, 0, 0}) : 1;
    buffer_text(&function->code, increment ? "add rax, " : "sub rax, ");
    buffer_number(&function->code, amount);
    buffer_char(&function->code, '\n');
    buffer_text(&function->code, "mov rdx, rax\nmov rcx, qword [rbp - ");
    buffer_number(&function->code, address_slot);
    buffer_text(&function->code, "]\n");
    emit_memory_store(&function->code, type, "rcx", NULL);
    if (postfix) {
        buffer_text(&function->code, "mov rax, qword [rbp - ");
        buffer_number(&function->code, old_slot);
        buffer_text(&function->code, "]\n");
        release_temp(function);
    }
    release_temp(function);
}

