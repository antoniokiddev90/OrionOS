#include "compiler.h"

typedef struct {
    long long value;
    char *label;
} SwitchCase;

typedef struct SwitchContext SwitchContext;
struct SwitchContext {
    SwitchCase *cases;
    size_t count;
    size_t capacity;
    char *default_label;
    char *break_label;
    SwitchContext *previous;
};

typedef struct {
    char *name;
    char *label;
    int defined;
} UserLabel;

static SwitchContext *active_switch;
static UserLabel user_labels[128];
static size_t user_label_count;

static UserLabel *find_user_label(const char *name) {
    for (size_t i = 0; i < user_label_count; ++i) {
        if (strcmp(user_labels[i].name, name) == 0) return &user_labels[i];
    }
    if (user_label_count == sizeof(user_labels) / sizeof(user_labels[0])) fail("too many labels in function");
    UserLabel *label = &user_labels[user_label_count++];
    label->name = copy_text(name);
    label->label = new_label();
    label->defined = 0;
    return label;
}

static void parse_switch_statement(void) {
    Function *function = active_function;
    expect('(', "opening parenthesis expected after switch");
    Node *expression = parse_expression(1);
    expect(')', "closing parenthesis expected after switch expression");
    generate_expression(function, expression);
    size_t value_slot = allocate_temp(function);
    emit_local_memory(&function->code, "mov", "qword", value_slot, NULL);
    SwitchContext context;
    memset(&context, 0, sizeof(context));
    context.break_label = new_label();
    context.previous = active_switch;
    active_switch = &context;
    if (function->loop_depth >= 64) fail("control nesting is too deep");
    function->break_labels[function->loop_depth] = context.break_label;
    function->continue_labels[function->loop_depth] = NULL;
    ++function->loop_depth;
    Buffer prefix = function->code;
    function->code = (Buffer){0};
    parse_statement();
    Buffer body = function->code;
    function->code = prefix;
    --function->loop_depth;
    active_switch = context.previous;
    for (size_t i = 0; i < context.count; ++i) {
        buffer_text(&function->code, "mov rax, qword [rbp - ");
        buffer_number(&function->code, value_slot);
        buffer_text(&function->code, "]\ncmp rax, ");
        buffer_signed(&function->code, context.cases[i].value);
        buffer_char(&function->code, '\n');
        emit_jump(&function->code, "je", context.cases[i].label);
    }
    emit_jump(&function->code, "jmp", context.default_label ? context.default_label : context.break_label);
    buffer_bytes(&function->code, body.data, body.size);
    emit_label(&function->code, context.break_label);
    release_temp(function);
    for (size_t i = 0; i < context.count; ++i) free(context.cases[i].label);
    free(context.cases);
    free(context.default_label);
    free(context.break_label);
    free(body.data);
}

static void parse_case_label(void) {
    if (!active_switch) fail("case label is outside a switch");
    Node *expression = parse_expression(2);
    long long value = constant_value(expression);
    expect(':', "colon expected after case value");
    for (size_t i = 0; i < active_switch->count; ++i) {
        if (active_switch->cases[i].value == value) fail("duplicate case value");
    }
    active_switch->cases = array_grow(active_switch->cases, &active_switch->capacity, active_switch->count + 1, sizeof(SwitchCase));
    active_switch->cases[active_switch->count].value = value;
    active_switch->cases[active_switch->count].label = new_label();
    emit_label(&active_function->code, active_switch->cases[active_switch->count].label);
    ++active_switch->count;
}

void enter_scope(Function *function) {
    if (function->scope_depth + 1 >= 64) fail("block nesting is too deep");
    ++function->scope_depth;
    function->scope_marks[function->scope_depth] = function->local_count;
}

void leave_scope(Function *function) {
    if (function->scope_depth <= 1) fail("invalid block scope");
    size_t mark = function->scope_marks[function->scope_depth];
    while (function->local_count > mark) free(function->locals[--function->local_count].name);
    --function->scope_depth;
}

void parse_compound_statement(void) {
    Function *function = active_function;
    expect('{', "opening brace expected");
    enter_scope(function);
    while (!accept('}')) {
        if (peek()->kind == T_EOF) fail("unterminated block");
        parse_statement();
    }
    leave_scope(function);
}

void parse_loop_body(const char *break_label, const char *continue_label) {
    Function *function = active_function;
    if (function->loop_depth >= 64) fail("loop nesting is too deep");
    function->break_labels[function->loop_depth] = (char *)break_label;
    function->continue_labels[function->loop_depth] = (char *)continue_label;
    ++function->loop_depth;
    parse_statement();
    --function->loop_depth;
}

void parse_if_statement(void) {
    Function *function = active_function;
    expect('(', "opening parenthesis expected after if");
    Node *condition = parse_expression(1);
    expect(')', "closing parenthesis expected after if condition");
    char *otherwise = new_label();
    char *done = new_label();
    generate_expression(function, condition);
    buffer_text(&function->code, "cmp rax, 0\n");
    emit_jump(&function->code, "je", otherwise);
    parse_statement();
    if (accept_word("else")) {
        emit_jump(&function->code, "jmp", done);
        emit_label(&function->code, otherwise);
        parse_statement();
        emit_label(&function->code, done);
    } else emit_label(&function->code, otherwise);
    free(otherwise);
    free(done);
}

void parse_while_statement(void) {
    Function *function = active_function;
    char *condition_label = new_label();
    char *done = new_label();
    emit_label(&function->code, condition_label);
    expect('(', "opening parenthesis expected after while");
    Node *condition = parse_expression(1);
    expect(')', "closing parenthesis expected after while condition");
    generate_expression(function, condition);
    buffer_text(&function->code, "cmp rax, 0\n");
    emit_jump(&function->code, "je", done);
    parse_loop_body(done, condition_label);
    emit_jump(&function->code, "jmp", condition_label);
    emit_label(&function->code, done);
    free(condition_label);
    free(done);
}

void parse_do_statement(void) {
    Function *function = active_function;
    char *start = new_label();
    char *condition_label = new_label();
    char *done = new_label();
    emit_label(&function->code, start);
    parse_loop_body(done, condition_label);
    emit_label(&function->code, condition_label);
    if (!accept_word("while")) fail("while expected after do body");
    expect('(', "opening parenthesis expected after do-while");
    Node *condition = parse_expression(1);
    expect(')', "closing parenthesis expected after do-while condition");
    expect(';', "semicolon expected after do-while");
    generate_expression(function, condition);
    buffer_text(&function->code, "cmp rax, 0\n");
    emit_jump(&function->code, "jne", start);
    emit_label(&function->code, done);
    free(start);
    free(condition_label);
    free(done);
}

void parse_for_statement(void) {
    Function *function = active_function;
    enter_scope(function);
    expect('(', "opening parenthesis expected after for");
    if (accept(';')) {
    } else if (is_type_start()) parse_local_declaration();
    else {
        Node *initializer = parse_expression(1);
        expect(';', "semicolon expected after for initializer");
        generate_expression(function, initializer);
    }
    char *condition_label = new_label();
    char *step_label = new_label();
    char *done = new_label();
    emit_label(&function->code, condition_label);
    Node *condition = NULL;
    if (!accept(';')) {
        condition = parse_expression(1);
        expect(';', "semicolon expected after for condition");
        generate_expression(function, condition);
        buffer_text(&function->code, "cmp rax, 0\n");
        emit_jump(&function->code, "je", done);
    }
    Node *step = NULL;
    if (!accept(')')) {
        step = parse_expression(1);
        expect(')', "closing parenthesis expected after for clauses");
    }
    parse_loop_body(done, step_label);
    emit_label(&function->code, step_label);
    if (step) generate_expression(function, step);
    emit_jump(&function->code, "jmp", condition_label);
    emit_label(&function->code, done);
    free(condition_label);
    free(step_label);
    free(done);
    leave_scope(function);
}

void parse_statement(void) {
    Function *function = active_function;
    if (peek()->kind == '{') {
        parse_compound_statement();
        return;
    }
    if (accept(';')) return;
    if (accept_word("if")) { parse_if_statement(); return; }
    if (accept_word("while")) { parse_while_statement(); return; }
    if (accept_word("do")) { parse_do_statement(); return; }
    if (accept_word("for")) { parse_for_statement(); return; }
    if (accept_word("switch")) { parse_switch_statement(); return; }
    if (accept_word("case")) { parse_case_label(); parse_statement(); return; }
    if (accept_word("default")) {
        if (!active_switch) fail("default label is outside a switch");
        if (active_switch->default_label) fail("duplicate default label");
        expect(':', "colon expected after default");
        active_switch->default_label = new_label();
        emit_label(&function->code, active_switch->default_label);
        parse_statement();
        return;
    }
    if (peek()->kind == T_IDENTIFIER && compiler.position + 1 < compiler.token_count && compiler.tokens[compiler.position + 1].kind == ':') {
        char *name = expect_identifier();
        expect(':', "colon expected after label");
        UserLabel *label = find_user_label(name);
        if (label->defined) fail("duplicate label");
        label->defined = 1;
        emit_label(&function->code, label->label);
        free(name);
        parse_statement();
        return;
    }
    if (accept_word("goto")) {
        char *name = expect_identifier();
        expect(';', "semicolon expected after goto");
        UserLabel *label = find_user_label(name);
        emit_jump(&function->code, "jmp", label->label);
        free(name);
        return;
    }
    if (accept_word("break")) {
        if (!function->loop_depth) fail("break is outside a loop");
        expect(';', "semicolon expected after break");
        emit_jump(&function->code, "jmp", function->break_labels[function->loop_depth - 1]);
        return;
    }
    if (accept_word("continue")) {
        size_t depth = function->loop_depth;
        while (depth && !function->continue_labels[depth - 1]) --depth;
        if (!depth) fail("continue is outside a loop");
        expect(';', "semicolon expected after continue");
        emit_jump(&function->code, "jmp", function->continue_labels[depth - 1]);
        return;
    }
    if (accept_word("return")) {
        if (!accept(';')) {
            Node *value = parse_expression(1);
            expect(';', "semicolon expected after return value");
            generate_expression(function, value);
        } else buffer_text(&function->code, "mov rax, 0\n");
        emit_jump(&function->code, "jmp", function->exit_label);
        return;
    }
    if (is_type_start()) {
        parse_local_declaration();
        return;
    }
    Node *expression = parse_expression(1);
    expect(';', "semicolon expected after expression");
    generate_expression(function, expression);
}

void parse_function_definition(const char *name, CType return_type, Parameter *parameters, size_t parameter_count, int is_static) {
    Function function;
    memset(&function, 0, sizeof(function));
    function.name = copy_text(name);
    function.return_type = return_type;
    function.is_static = is_static;
    function.exit_label = new_label();
    function.scope_depth = 1;
    active_function = &function;
    user_label_count = 0;
    active_switch = NULL;
    for (size_t i = 0; i < parameter_count; ++i) {
        if (!parameters[i].name) fail("function parameter name is required in a definition");
        add_local(&function, parameters[i].name, parameters[i].type);
    }
    expect('{', "opening brace expected before function body");
    while (!accept('}')) {
        if (peek()->kind == T_EOF) fail("unterminated function body");
        parse_statement();
    }
    for (size_t i = 0; i < user_label_count; ++i) {
        if (!user_labels[i].defined) fail("goto references an undefined label");
    }
    if (strcmp(name, "_start") == 0) compiler.saw_start = 1;
    finish_function(&function, parameters, parameter_count);
    while (function.local_count) free(function.locals[--function.local_count].name);
    free(function.locals);
    free(function.code.data);
    free(function.name);
    free(function.exit_label);
    for (size_t i = 0; i < user_label_count; ++i) {
        free(user_labels[i].name);
        free(user_labels[i].label);
    }
    user_label_count = 0;
    active_function = NULL;
}
