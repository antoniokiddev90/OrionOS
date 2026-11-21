#ifndef ORION_CC_COMPILER_H
#define ORION_CC_COMPILER_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} Buffer;

typedef struct {
    int kind;
    char *text;
    size_t length;
    long long value;
    unsigned line;
} Token;

typedef struct {
    int base;
    int pointers;
    int is_unsigned;
    int is_array;
    size_t count;
} CType;

typedef struct {
    char *name;
    char *replacement;
} Macro;

typedef struct {
    char *name;
    CType type;
} Alias;

typedef struct {
    char *name;
    CType type;
    size_t offset;
    int global;
    int defined;
    size_t scope_depth;
    long long constant_value;
} Variable;

typedef struct Node Node;
struct Node {
    int kind;
    int op;
    long long value;
    char *text;
    size_t length;
    CType type;
    Node *left;
    Node *right;
    Node *third;
    Node **arguments;
    size_t argument_count;
    size_t argument_capacity;
};

typedef struct {
    Buffer code;
    Variable *locals;
    size_t local_count;
    size_t local_capacity;
    size_t local_bytes;
    size_t temp_depth;
    size_t max_temp_depth;
    size_t scope_marks[64];
    size_t scope_depth;
    char *exit_label;
    char *break_labels[64];
    char *continue_labels[64];
    size_t loop_depth;
    char *name;
    CType return_type;
    int is_static;
} Function;

typedef struct {
    Token *tokens;
    size_t token_count;
    size_t token_capacity;
    size_t position;
    Macro *macros;
    size_t macro_count;
    size_t macro_capacity;
    Alias *aliases;
    size_t alias_count;
    size_t alias_capacity;
    Variable *globals;
    size_t global_count;
    size_t global_capacity;
    Buffer text;
    Buffer rodata;
    Buffer data;
    Buffer bss;
    unsigned label_number;
    const char *source_name;
    const char *program_path;
    const char *assembler_path;
    const char *linker_path;
    const char *script_path;
    int saw_start;
} Compiler;

enum {
    T_EOF = 256,
    T_IDENTIFIER,
    T_NUMBER,
    T_STRING,
    OP_EQ = 512,
    OP_NE,
    OP_LE,
    OP_GE,
    OP_SHL,
    OP_SHR,
    OP_ANDAND,
    OP_OROR,
    OP_ADD_ASSIGN,
    OP_SUB_ASSIGN,
    OP_MUL_ASSIGN,
    OP_DIV_ASSIGN,
    OP_MOD_ASSIGN,
    OP_AND_ASSIGN,
    OP_OR_ASSIGN,
    OP_XOR_ASSIGN,
    OP_SHL_ASSIGN,
    OP_SHR_ASSIGN,
    OP_INC,
    OP_DEC
};

enum {
    TY_VOID = 1,
    TY_BOOL,
    TY_CHAR,
    TY_SHORT,
    TY_INT,
    TY_LONG
};

enum {
    N_NUMBER = 1,
    N_STRING,
    N_IDENTIFIER,
    N_UNARY,
    N_BINARY,
    N_ASSIGN,
    N_CALL,
    N_INDEX,
    N_CONDITIONAL,
    N_CAST,
    N_PREINC,
    N_PREDEC,
    N_POSTINC,
    N_POSTDEC
};


typedef struct {
    char *name;
    CType type;
} Declarator;

typedef struct {
    char *name;
    CType type;
} Parameter;

typedef struct {
    Node **items;
    size_t count;
    size_t capacity;
    Node *string_value;
} Initializer;

extern Compiler compiler;
extern Function *active_function;

void fail(const char *message);
void *grow_memory(void *memory, size_t old_size, size_t new_size);
char *copy_range(const char *text, size_t length);
char *copy_text(const char *text);
void buffer_reserve(Buffer *buffer, size_t additional);
void buffer_bytes(Buffer *buffer, const char *data, size_t length);
void buffer_text(Buffer *buffer, const char *text);
void buffer_char(Buffer *buffer, char value);
void buffer_number(Buffer *buffer, unsigned long long value);
void buffer_signed(Buffer *buffer, long long value);
int is_space(char value);
int is_digit(char value);
int is_alpha(char value);
int is_alnum(char value);
void *array_grow(void *memory, size_t *capacity, size_t need, size_t element_size);
Macro *find_macro(const char *name);
void define_macro(const char *name, const char *replacement);
void expand_line(Buffer *output, const char *line);
char *preprocess(const char *source);
int decode_escape(const char **cursor);
void add_token(int kind, const char *text, size_t length, long long value, unsigned line);
long long parse_integer(const char *text, size_t length);
int match_operator(const char *cursor, const char **text, int *kind);
void tokenize(const char *source);
Token *peek(void);
int accept(int kind);
int accept_word(const char *word);
void expect(int kind, const char *message);
char *expect_identifier(void);
int type_keyword(const char *name);
Alias *find_alias(const char *name);
int is_type_start(void);
size_t type_size(CType type);
size_t align_value(size_t value, size_t alignment);
CType parse_type_specifier(void);
CType parse_pointer_type(CType type);
void add_alias(const char *name, CType type);
void add_enum_constant(const char *name, long long value);
int find_enum_constant(const char *name, long long *value);
void initialize_builtin_aliases(void);
Node *new_node(int kind, int op, Node *left, Node *right);
Node *number_node(long long value);
int lookup_variable(const char *name, Variable *result);
CType node_type(Node *node);
int assignment_operator(int kind);
int precedence(int kind);
Node *parse_primary(void);
Node *parse_postfix(void);
Node *parse_unary(void);
Node *parse_expression(int minimum_precedence);
char *new_label(void);
void emit_label(Buffer *buffer, const char *label);
void emit_jump(Buffer *buffer, const char *instruction, const char *label);
void emit_local_memory(Buffer *buffer, const char *instruction, const char *left, size_t offset, const char *right);
size_t allocate_temp(Function *function);
void release_temp(Function *function);
Variable *add_local(Function *function, const char *name, CType type);
Variable *add_global(const char *name, CType type, int defined);
void emit_memory_load(Buffer *buffer, CType type, const char *address_register);
void emit_memory_store(Buffer *buffer, CType type, const char *address_register, const char *value_register);
void emit_normalize_bool(Buffer *buffer);
void generate_address(Function *function, Node *node);
void generate_load_from_address(Function *function, CType type);
void generate_compare(Function *function, Node *node);
void generate_logical(Function *function, Node *node);
void generate_call(Function *function, Node *node);
void generate_assignment(Function *function, Node *node);
void generate_binary(Function *function, Node *node);
void generate_update(Function *function, Node *node, int increment, int postfix);
void generate_expression(Function *function, Node *node);
int peek_word(const char *word);
void parse_storage(int *is_typedef, int *is_static, int *is_extern);
Initializer parse_initializer(void);
void emit_string_data(Node *string_node, char **result_label);
long long constant_value(Node *node);
void emit_global_initializer(const char *name, CType type, Initializer *initializer, int is_static);
void add_function_symbol(const char *name, CType type, int defined);
void emit_parameter_store(Buffer *output, size_t parameter_index, Variable *variable);
void finish_function(Function *function, Parameter *parameters, size_t parameter_count);
Node *variable_node(const char *name);
void emit_local_array_initializer(Function *function, Variable *variable, Initializer *initializer);
void parse_local_declaration(void);
void enter_scope(Function *function);
void leave_scope(Function *function);
void parse_compound_statement(void);
void parse_loop_body(const char *break_label, const char *continue_label);
void parse_if_statement(void);
void parse_while_statement(void);
void parse_do_statement(void);
void parse_for_statement(void);
void parse_statement(void);
Parameter *parse_parameters(size_t *parameter_count);
Declarator parse_declarator_from_base(CType base, int allow_missing_name);
void parse_function_definition(const char *name, CType return_type, Parameter *parameters, size_t parameter_count, int is_static);
void parse_external_declaration(void);
void parse_translation_unit(void);
char *read_file(const char *path, size_t *size);
void write_file(const char *path, const char *data, size_t size);
char *path_with_suffix(const char *path, const char *suffix);
char *default_output_name(const char *source, const char *suffix);
char *tool_path(const char *tool);
int run_tool(char *const arguments[]);
void append_start_wrapper(void);
void assemble_source(const char *source_path, const char *object_path);
void link_objects(const char *output_path, const char *object_path, char **extra_objects, size_t extra_count);
void print_help(void);
int main(int argc, char **argv);

#endif
