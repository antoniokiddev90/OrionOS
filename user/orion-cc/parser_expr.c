#include "compiler.h"

int assignment_operator(int kind) {
    return kind == '=' || kind == OP_ADD_ASSIGN || kind == OP_SUB_ASSIGN || kind == OP_MUL_ASSIGN || kind == OP_DIV_ASSIGN || kind == OP_MOD_ASSIGN || kind == OP_AND_ASSIGN || kind == OP_OR_ASSIGN || kind == OP_XOR_ASSIGN || kind == OP_SHL_ASSIGN || kind == OP_SHR_ASSIGN;
}

int precedence(int kind) {
    if (kind == ',') return 1;
    if (assignment_operator(kind)) return 2;
    if (kind == '?') return 3;
    if (kind == OP_OROR) return 4;
    if (kind == OP_ANDAND) return 5;
    if (kind == '|') return 6;
    if (kind == '^') return 7;
    if (kind == '&') return 8;
    if (kind == OP_EQ || kind == OP_NE) return 9;
    if (kind == '<' || kind == '>' || kind == OP_LE || kind == OP_GE) return 10;
    if (kind == OP_SHL || kind == OP_SHR) return 11;
    if (kind == '+' || kind == '-') return 12;
    if (kind == '*' || kind == '/' || kind == '%') return 13;
    return 0;
}

Node *parse_primary(void) {
    if (peek()->kind == T_NUMBER) {
        long long value = compiler.tokens[compiler.position++].value;
        return number_node(value);
    }
    if (peek()->kind == T_STRING) {
        Token *token = &compiler.tokens[compiler.position++];
        Node *node = new_node(N_STRING, 0, NULL, NULL);
        node->text = copy_range(token->text, token->length);
        node->length = token->length;
        node->type = (CType){TY_CHAR, 1, 0, 0, 0};
        return node;
    }
    if (peek()->kind == T_IDENTIFIER) {
        long long enum_value = 0;
        if (find_enum_constant(peek()->text, &enum_value)) {
            ++compiler.position;
            return number_node(enum_value);
        }
        if (strcmp(peek()->text, "true") == 0 || strcmp(peek()->text, "false") == 0 || strcmp(peek()->text, "NULL") == 0) {
            long long value = strcmp(peek()->text, "true") == 0;
            ++compiler.position;
            return number_node(value);
        }
        Node *node = new_node(N_IDENTIFIER, 0, NULL, NULL);
        node->text = expect_identifier();
        node->type = node_type(node);
        return node;
    }
    if (accept('(')) {
        Node *node = parse_expression(1);
        expect(')', "closing parenthesis expected");
        return node;
    }
    fail("expression expected");
    return NULL;
}

Node *parse_postfix(void) {
    Node *node = parse_primary();
    for (;;) {
        if (accept('(')) {
            if (node->kind != N_IDENTIFIER) fail("only named functions can be called");
            Node *call = new_node(N_CALL, 0, node, NULL);
            call->type = node_type(node);
            if (!accept(')')) {
                for (;;) {
                    call->arguments = array_grow(call->arguments, &call->argument_capacity, call->argument_count + 1, sizeof(Node *));
                    call->arguments[call->argument_count++] = parse_expression(2);
                    if (accept(')')) break;
                    expect(',', "comma expected between arguments");
                }
            }
            node = call;
            continue;
        }
        if (accept('[')) {
            Node *index = parse_expression(1);
            expect(']', "closing bracket expected");
            node = new_node(N_INDEX, 0, node, index);
            node->type = node_type(node);
            continue;
        }
        if (accept(OP_INC)) {
            Node *updated = new_node(N_POSTINC, 0, node, NULL);
            updated->type = node_type(node);
            node = updated;
            continue;
        }
        if (accept(OP_DEC)) {
            Node *updated = new_node(N_POSTDEC, 0, node, NULL);
            updated->type = node_type(node);
            node = updated;
            continue;
        }
        break;
    }
    return node;
}

Node *parse_unary(void) {
    if (accept('+')) return new_node(N_UNARY, '+', parse_unary(), NULL);
    if (accept('-')) return new_node(N_UNARY, '-', parse_unary(), NULL);
    if (accept('!')) return new_node(N_UNARY, '!', parse_unary(), NULL);
    if (accept('~')) return new_node(N_UNARY, '~', parse_unary(), NULL);
    if (accept('*')) return new_node(N_UNARY, '*', parse_unary(), NULL);
    if (accept('&')) return new_node(N_UNARY, '&', parse_unary(), NULL);
    if (accept(OP_INC)) return new_node(N_PREINC, 0, parse_unary(), NULL);
    if (accept(OP_DEC)) return new_node(N_PREDEC, 0, parse_unary(), NULL);
    if (peek()->kind == T_IDENTIFIER && strcmp(peek()->text, "sizeof") == 0) {
        ++compiler.position;
        Node *operand = NULL;
        if (accept('(')) {
            if (is_type_start()) {
                CType type = parse_pointer_type(parse_type_specifier());
                expect(')', "closing parenthesis expected after sizeof type");
                return number_node((long long)type_size(type));
            }
            operand = parse_expression(1);
            expect(')', "closing parenthesis expected after sizeof expression");
        } else {
            operand = parse_unary();
        }
        if (operand->kind == N_IDENTIFIER) {
            Variable variable;
            if (lookup_variable(operand->text, &variable) && variable.type.is_array) {
                CType element = variable.type;
                element.is_array = 0;
                return number_node((long long)(variable.type.count * type_size(element)));
            }
        }
        return number_node((long long)type_size(node_type(operand)));
    }
    if (peek()->kind == '(' && compiler.position + 1 < compiler.token_count) {
        Token *next = &compiler.tokens[compiler.position + 1];
        if (next->kind == T_IDENTIFIER && (type_keyword(next->text) || find_alias(next->text))) {
            ++compiler.position;
            CType type = parse_pointer_type(parse_type_specifier());
            expect(')', "closing parenthesis expected after cast");
            Node *cast = new_node(N_CAST, 0, parse_unary(), NULL);
            cast->type = type;
            return cast;
        }
    }
    return parse_postfix();
}

Node *parse_expression(int minimum_precedence) {
    Node *left = parse_unary();
    for (;;) {
        int op = peek()->kind;
        int level = precedence(op);
        if (level < minimum_precedence || !level) break;
        ++compiler.position;
        if (op == '?') {
            Node *when_true = parse_expression(1);
            expect(':', "colon expected in conditional expression");
            Node *when_false = parse_expression(level);
            Node *conditional = new_node(N_CONDITIONAL, op, left, when_true);
            conditional->third = when_false;
            conditional->type = node_type(left);
            left = conditional;
            continue;
        }
        int right_minimum = assignment_operator(op) ? level : level + 1;
        Node *right = parse_expression(right_minimum);
        if (assignment_operator(op)) left = new_node(N_ASSIGN, op, left, right);
        else left = new_node(N_BINARY, op, left, right);
    }
    return left;
}
