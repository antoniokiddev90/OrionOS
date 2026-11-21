#include "compiler.h"

Node *new_node(int kind, int op, Node *left, Node *right) {
    Node *node = malloc(sizeof(Node));
    if (!node) fail("out of memory");
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->op = op;
    node->left = left;
    node->right = right;
    return node;
}

Node *number_node(long long value) {
    Node *node = new_node(N_NUMBER, 0, NULL, NULL);
    node->value = value;
    node->type = (CType){TY_INT, 0, 0, 0, 0};
    return node;
}

Node *variable_node(const char *name) {
    Node *node = new_node(N_IDENTIFIER, 0, NULL, NULL);
    node->text = copy_text(name);
    node->type = node_type(node);
    return node;
}

