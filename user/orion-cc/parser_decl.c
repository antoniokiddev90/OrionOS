#include "compiler.h"

Parameter *parse_parameters(size_t *parameter_count) {
    Parameter *parameters = NULL;
    size_t capacity = 0;
    *parameter_count = 0;
    if (accept(')')) return parameters;
    if (peek_word("void") && compiler.position + 1 < compiler.token_count && compiler.tokens[compiler.position + 1].kind == ')') {
        ++compiler.position;
        ++compiler.position;
        return parameters;
    }
    for (;;) {
        CType type = parse_pointer_type(parse_type_specifier());
        char *name = peek()->kind == T_IDENTIFIER ? expect_identifier() : NULL;
        if (accept('[')) {
            if (peek()->kind == T_NUMBER) ++compiler.position;
            expect(']', "closing bracket expected in parameter");
            type.is_array = 0;
            ++type.pointers;
        }
        parameters = array_grow(parameters, &capacity, *parameter_count + 1, sizeof(Parameter));
        parameters[*parameter_count].name = name;
        parameters[*parameter_count].type = type;
        ++*parameter_count;
        if (accept(')')) break;
        expect(',', "comma expected between parameters");
    }
    return parameters;
}

Declarator parse_declarator_from_base(CType base, int allow_missing_name) {
    Declarator declarator;
    memset(&declarator, 0, sizeof(declarator));
    CType type = parse_pointer_type(base);
    if (peek()->kind == T_IDENTIFIER) declarator.name = expect_identifier();
    else if (!allow_missing_name) fail("declarator name expected");
    if (accept('[')) {
        type.is_array = 1;
        if (peek()->kind == T_NUMBER) {
            long long count = peek()->value;
            ++compiler.position;
            if (count < 0 || count > 0x100000) fail("array size is invalid");
            type.count = (size_t)count;
        }
        expect(']', "closing bracket expected after array size");
    }
    declarator.type = type;
    return declarator;
}

void parse_external_declaration(void) {
    int is_typedef = 0;
    int is_static = 0;
    int is_extern = 0;
    parse_storage(&is_typedef, &is_static, &is_extern);
    CType base = parse_type_specifier();
    if (accept(';')) return;
    for (;;) {
        Declarator declarator = parse_declarator_from_base(base, 0);
        if (!is_typedef && accept('(')) {
            size_t parameter_count = 0;
            Parameter *parameters = parse_parameters(&parameter_count);
            add_function_symbol(declarator.name, declarator.type, 0);
            if (peek()->kind == '{') {
                add_function_symbol(declarator.name, declarator.type, 1);
                parse_function_definition(declarator.name, declarator.type, parameters, parameter_count, is_static);
                free(declarator.name);
                return;
            }
            free(parameters);
            if (accept(',')) {
                free(declarator.name);
                continue;
            }
            expect(';', "semicolon expected after function declaration");
            free(declarator.name);
            return;
        }
        if (is_typedef) add_alias(declarator.name, declarator.type);
        else if (is_extern && peek()->kind != '=') {
            Variable *variable = add_global(declarator.name, declarator.type, 0);
            variable->global = 1;
            buffer_text(&compiler.text, "extern ");
            buffer_text(&compiler.text, declarator.name);
            buffer_char(&compiler.text, '\n');
        } else {
            Initializer initializer;
            memset(&initializer, 0, sizeof(initializer));
            if (accept('=')) initializer = parse_initializer();
            if (declarator.type.is_array && !declarator.type.count) {
                if (initializer.string_value) declarator.type.count = initializer.string_value->length + 1;
                else if (initializer.count) declarator.type.count = initializer.count;
                else fail("array size cannot be inferred");
            }
            if (declarator.type.is_array && initializer.count > declarator.type.count) fail("too many array initializers");
            emit_global_initializer(declarator.name, declarator.type, &initializer, is_static);
            free(initializer.items);
            if (initializer.string_value) {
                free(initializer.string_value->text);
                free(initializer.string_value);
            }
        }
        free(declarator.name);
        if (!accept(',')) break;
    }
    expect(';', "semicolon expected after declaration");
}

void parse_translation_unit(void) {
    while (peek()->kind != T_EOF) parse_external_declaration();
}
