#include "as.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    int code;
    int width;
    int high_byte;
    int segment;
    int control;
} AsRegister;

typedef struct {
    int base;
    int rip_relative;
    int explicit_size;
    int64_t displacement;
    int symbol;
    int64_t addend;
} AsMemory;

static char *trim_operand(char *text) {
    while (*text == ' ' || *text == '\t') ++text;
    size_t length = strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t')) text[--length] = 0;
    return text;
}

static int word_equal(const char *left, const char *right) {
    return strcmp(left, right) == 0;
}

static int register_lookup(const char *name, AsRegister *reg) {
    static const char *names64[16] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
    static const char *names32[16] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"};
    static const char *names16[16] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di", "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"};
    static const char *names8[16] = {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"};
    for (int i = 0; i < 16; ++i) {
        if (word_equal(name, names64[i])) { *reg = (AsRegister){i, 64, 0, 0, 0}; return 1; }
        if (word_equal(name, names32[i])) { *reg = (AsRegister){i, 32, 0, 0, 0}; return 1; }
        if (word_equal(name, names16[i])) { *reg = (AsRegister){i, 16, 0, 0, 0}; return 1; }
        if (word_equal(name, names8[i])) { *reg = (AsRegister){i, 8, 0, 0, 0}; return 1; }
    }
    static const char *low8[4] = {"al", "cl", "dl", "bl"};
    static const char *high8[4] = {"ah", "ch", "dh", "bh"};
    for (int i = 0; i < 4; ++i) {
        if (word_equal(name, low8[i])) { *reg = (AsRegister){i, 8, 0, 0, 0}; return 1; }
        if (word_equal(name, high8[i])) { *reg = (AsRegister){i + 4, 8, 1, 0, 0}; return 1; }
    }
    static const char *segments[6] = {"es", "cs", "ss", "ds", "fs", "gs"};
    for (int i = 0; i < 6; ++i) if (word_equal(name, segments[i])) { *reg = (AsRegister){i, 16, 0, 1, 0}; return 1; }
    static const char *controls[5] = {"cr0", "cr2", "cr3", "cr4", "cr8"};
    static const int control_codes[5] = {0, 2, 3, 4, 8};
    for (int i = 0; i < 5; ++i) if (word_equal(name, controls[i])) { *reg = (AsRegister){control_codes[i], 64, 0, 0, 1}; return 1; }
    return 0;
}

static void emit_byte(Assembler *assembler, unsigned char value) {
    as_emit_byte(assembler, value);
}

static void emit_rex(Assembler *assembler, int width, int reg, int index, int base, int force) {
    unsigned char rex = 0x40;
    if (width == 64) rex |= 8;
    if (reg >= 8) rex |= 4;
    if (index >= 8) rex |= 2;
    if (base >= 8) rex |= 1;
    if (rex != 0x40 || force) emit_byte(assembler, rex);
}

static uint64_t current_offset(Assembler *assembler) {
    AsSection *section = &assembler->sections[assembler->current_section];
    return section->type == 8 ? section->bss_size : section->bytes.size;
}

static void emit_relocation(Assembler *assembler, int symbol, uint32_t type, int64_t addend, size_t width) {
    if (symbol >= 0 && assembler->pass == 2) as_add_relocation(assembler, assembler->symbols[symbol].name, type, addend);
    as_emit_zeros(assembler, width);
}

static void emit_expression_value(Assembler *assembler, char *text, size_t width, uint32_t relocation_type, int64_t relocation_adjustment) {
    int64_t value;
    int symbol;
    int64_t addend;
    if (as_expression(assembler, text, &value, &symbol, &addend) != 0) as_fail("invalid operand expression");
    if (symbol >= 0) {
        if (!relocation_type) as_fail("unsupported relocation");
        emit_relocation(assembler, symbol, relocation_type, addend + relocation_adjustment, width);
    } else {
        as_emit_integer(assembler, (uint64_t)value, width);
    }
}

static int split_operands(char *text, char **left, char **right) {
    int bracket = 0;
    int quoted = 0;
    for (char *cursor = text; *cursor; ++cursor) {
        if (*cursor == '"' && (cursor == text || cursor[-1] != '\\')) quoted = !quoted;
        if (!quoted) {
            if (*cursor == '[') ++bracket;
            if (*cursor == ']') --bracket;
            if (*cursor == ',' && !bracket) {
                *cursor = 0;
                *left = trim_operand(text);
                *right = trim_operand(cursor + 1);
                return 2;
            }
        }
    }
    *left = trim_operand(text);
    *right = NULL;
    return 1;
}

static int parse_qualifier(char **operand) {
    char *text = trim_operand(*operand);
    struct Pair { const char *name; int size; };
    static const struct Pair sizes[] = {{"byte", 8}, {"word", 16}, {"dword", 32}, {"qword", 64}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        size_t length = strlen(sizes[i].name);
        if (strncmp(text, sizes[i].name, length) == 0 && (text[length] == ' ' || text[length] == '\t')) {
            *operand = trim_operand(text + length);
            return sizes[i].size;
        }
    }
    *operand = text;
    return 0;
}

static int parse_memory(Assembler *assembler, char *operand, AsMemory *memory) {
    memset(memory, 0, sizeof(*memory));
    memory->base = -1;
    memory->symbol = -1;
    char *text = trim_operand(operand);
    memory->explicit_size = parse_qualifier(&text);
    char *left = strchr(text, '[');
    char *right = NULL;
    for (char *cursor = text; *cursor; ++cursor) if (*cursor == ']') right = cursor;
    if (!left || !right || right <= left) return 0;
    *right = 0;
    char expression[256];
    size_t length = strlen(left + 1);
    if (length >= sizeof(expression)) as_fail("memory operand is too long");
    memcpy(expression, left + 1, length + 1);
    char *inside = trim_operand(expression);
    if (strncmp(inside, "rel ", 4) == 0) {
        memory->rip_relative = 1;
        inside = trim_operand(inside + 4);
    }
    if (strncmp(inside, "rip", 3) == 0 && (inside[3] == 0 || inside[3] == '+' || inside[3] == '-')) {
        memory->rip_relative = 1;
        inside = trim_operand(inside + 3);
        if (*inside == '+') inside = trim_operand(inside + 1);
    }
    if (memory->rip_relative) {
        if (*inside == '-' || *inside == '+') {
            if (*inside == '+') ++inside;
        }
        int64_t value;
        int symbol;
        int64_t addend;
        if (*inside && as_expression(assembler, inside, &value, &symbol, &addend) != 0) as_fail("invalid relative memory operand");
        else if (!*inside) { value = 0; symbol = -1; addend = 0; }
        memory->displacement = value;
        memory->symbol = symbol;
        memory->addend = addend;
        return 1;
    }
    char *cursor = inside;
    while (*cursor && *cursor != '+' && *cursor != '-') ++cursor;
    char saved = *cursor;
    *cursor = 0;
    AsRegister base;
    if (register_lookup(trim_operand(inside), &base) && !base.segment && !base.control && base.width >= 32) {
        memory->base = base.code;
        *cursor = saved;
        char *displacement = trim_operand(cursor);
        if (*displacement == '+') displacement = trim_operand(displacement + 1);
        if (*displacement) {
            int64_t value;
            int symbol;
            int64_t addend;
            if (as_expression(assembler, displacement, &value, &symbol, &addend) != 0) as_fail("unsupported memory expression");
            memory->displacement = value;
            memory->symbol = symbol;
            memory->addend = addend;
        }
        return 1;
    }
    *cursor = saved;
    int64_t value;
    int symbol;
    int64_t addend;
    if (as_expression(assembler, inside, &value, &symbol, &addend) != 0) as_fail("unsupported memory expression");
    memory->displacement = value;
    memory->symbol = symbol;
    memory->addend = addend;
    return 1;
}

static void emit_memory_tail(Assembler *assembler, const AsMemory *memory, int reg_field, int width, int force_rex, int relocation_kind) {
    int mod;
    int base = memory->base;
    int rm;
    int displacement_size;
    if (memory->rip_relative || base < 0) {
        mod = 0;
        rm = 5;
        displacement_size = 4;
    } else if (memory->displacement == 0 && (base & 7) != 5 && memory->symbol < 0) {
        mod = 0;
        rm = base & 7;
        displacement_size = 0;
    } else if (memory->symbol < 0 && memory->displacement >= -128 && memory->displacement <= 127) {
        mod = 1;
        rm = base & 7;
        displacement_size = 1;
    } else {
        mod = 2;
        rm = base < 0 ? 5 : base & 7;
        displacement_size = 4;
    }
    if (base >= 0 && ((base & 7) == 4)) rm = 4;
    emit_byte(assembler, (unsigned char)((mod << 6) | ((reg_field & 7) << 3) | rm));
    if (rm == 4 && base >= 0) emit_byte(assembler, (unsigned char)(0x20 | (base & 7)));
    if (!displacement_size) return;
    if (memory->symbol >= 0) {
        uint32_t type = relocation_kind == 1 || memory->rip_relative ? 2 : assembler->mode_bits == 64 ? 11 : 10;
        int64_t adjustment = memory->rip_relative ? -4 : 0;
        emit_relocation(assembler, memory->symbol, type, memory->addend + adjustment, 4);
    } else if (displacement_size == 1) {
        emit_byte(assembler, (unsigned char)memory->displacement);
    } else {
        as_emit_integer(assembler, (uint32_t)memory->displacement, 4);
    }
    (void)width;
    (void)force_rex;
}

static void emit_reg_rm(Assembler *assembler, unsigned char opcode, AsRegister reg, const AsMemory *memory, int width) {
    int force_rex = width == 8 && (reg.code >= 4 || reg.high_byte == 0);
    if (reg.high_byte && width == 8 && reg.code >= 4) force_rex = 0;
    if (width == 16) emit_byte(assembler, 0x66);
    emit_rex(assembler, width, reg.code, 0, memory->base, force_rex);
    emit_byte(assembler, opcode);
    emit_memory_tail(assembler, memory, reg.code, width, force_rex, 0);
}

static void emit_reg_reg(Assembler *assembler, unsigned char opcode, AsRegister reg_field, AsRegister rm_field, int width, int force_rex) {
    if (width == 16) emit_byte(assembler, 0x66);
    int need_rex = force_rex || (width == 64) || reg_field.code >= 8 || rm_field.code >= 8 || ((reg_field.code >= 4 || rm_field.code >= 4) && width == 8 && !reg_field.high_byte && !rm_field.high_byte);
    if ((reg_field.high_byte || rm_field.high_byte) && need_rex) as_fail("high-byte register cannot be used with a REX prefix");
    emit_rex(assembler, width, reg_field.code, 0, rm_field.code, need_rex);
    emit_byte(assembler, opcode);
    emit_byte(assembler, (unsigned char)(0xc0 | ((reg_field.code & 7) << 3) | (rm_field.code & 7)));
}

static int condition_code(const char *name) {
    static const char *names[16][4] = {
        {"jo", NULL, NULL, NULL}, {"jno", NULL, NULL, NULL}, {"jb", "jc", "jnae", NULL}, {"jae", "jnb", "jnc", NULL},
        {"je", "jz", NULL, NULL}, {"jne", "jnz", NULL, NULL}, {"jbe", "jna", NULL, NULL}, {"ja", "jnbe", NULL, NULL},
        {"js", NULL, NULL, NULL}, {"jns", NULL, NULL, NULL}, {"jp", "jpe", NULL, NULL}, {"jnp", "jpo", NULL, NULL},
        {"jl", "jnge", NULL, NULL}, {"jge", "jnl", NULL, NULL}, {"jle", "jng", NULL, NULL}, {"jg", "jnle", NULL, NULL}
    };
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 4 && names[i][j]; ++j) if (strcmp(name, names[i][j]) == 0) return i;
    return -1;
}

static void emit_branch(Assembler *assembler, const char *instruction, char *operand) {
    int64_t value;
    int symbol;
    int64_t addend;
    if (as_expression(assembler, operand, &value, &symbol, &addend) != 0) as_fail("invalid branch target");
    if (strcmp(instruction, "loop") == 0) {
        emit_byte(assembler, 0xe2);
        if (symbol < 0) emit_byte(assembler, (unsigned char)value);
        else {
            AsSymbol *target = &assembler->symbols[symbol];
            if (!target->defined || target->section != assembler->current_section + 1) as_fail("loop target must be in the same section");
            int64_t displacement = (int64_t)target->value + addend - (int64_t)(current_offset(assembler) + 1);
            if (displacement < -128 || displacement > 127) as_fail("loop target is out of range");
            emit_byte(assembler, (unsigned char)displacement);
        }
        return;
    }
    if (strcmp(instruction, "call") == 0 || strcmp(instruction, "jmp") == 0) {
        emit_byte(assembler, strcmp(instruction, "call") == 0 ? 0xe8 : 0xe9);
        if (symbol >= 0) emit_relocation(assembler, symbol, 4, addend - 4, 4);
        else as_emit_integer(assembler, (uint32_t)(value - (int64_t)(current_offset(assembler) + 4)), 4);
        return;
    }
    int condition = condition_code(instruction);
    if (condition < 0) as_fail("unsupported branch instruction");
    emit_byte(assembler, 0x0f);
    emit_byte(assembler, (unsigned char)(0x80 + condition));
    if (symbol >= 0) emit_relocation(assembler, symbol, 2, addend - 4, 4);
    else as_emit_integer(assembler, (uint32_t)(value - (int64_t)(current_offset(assembler) + 4)), 4);
}

static void emit_far_jump(Assembler *assembler, char *operand) {
    char *separator = strchr(operand, ':');
    if (!separator) as_fail("invalid far jump target");
    *separator++ = 0;
    char *segment = trim_operand(operand);
    char *target = trim_operand(separator);
    int64_t value;
    int symbol;
    int64_t addend;
    int64_t selector;
    int selector_symbol;
    int64_t selector_addend;
    if (as_expression(assembler, target, &value, &symbol, &addend) != 0 || as_expression(assembler, segment, &selector, &selector_symbol, &selector_addend) != 0 || selector_symbol >= 0) as_fail("invalid far jump target");
    emit_byte(assembler, 0xea);
    if (symbol >= 0) emit_relocation(assembler, symbol, 10, addend, 4);
    else as_emit_integer(assembler, (uint32_t)value, 4);
    as_emit_integer(assembler, (uint16_t)selector, 2);
}

static void emit_move(Assembler *assembler, char *left, char *right) {
    int left_size = parse_qualifier(&left);
    int right_size = parse_qualifier(&right);
    AsRegister destination;
    AsRegister source;
    int left_is_register = register_lookup(left, &destination);
    int right_is_register = register_lookup(right, &source);
    AsMemory destination_memory;
    AsMemory source_memory;
    int left_is_memory = !left_is_register && parse_memory(assembler, left, &destination_memory);
    int right_is_memory = !right_is_register && parse_memory(assembler, right, &source_memory);
    if (left_is_register && right_is_register) {
        if (destination.control || source.control) {
            if (destination.control == source.control) as_fail("invalid control register move");
            AsRegister control = destination.control ? destination : source;
            AsRegister general = destination.control ? source : destination;
            if (general.width != 32 || general.segment || general.control) as_fail("control register moves require a 32-bit register");
            emit_rex(assembler, 0, control.code, 0, general.code, control.code >= 8 || general.code >= 8);
            emit_byte(assembler, 0x0f);
            emit_byte(assembler, destination.control ? 0x20 : 0x22);
            emit_byte(assembler, (unsigned char)(0xc0 | ((control.code & 7) << 3) | (general.code & 7)));
            return;
        }
        if (destination.segment || source.segment) {
            AsRegister segment = destination.segment ? destination : source;
            AsRegister general = destination.segment ? source : destination;
            if (!destination.segment && !source.segment) as_fail("invalid segment register move");
            if (general.width != 16) as_fail("segment register moves require a 16-bit register");
            emit_byte(assembler, 0x8e);
            emit_byte(assembler, (unsigned char)(0xc0 | ((segment.code & 7) << 3) | (general.code & 7)));
            return;
        }
        if (destination.width != source.width) as_fail("register sizes do not match");
        emit_reg_reg(assembler, 0x89, source, destination, destination.width, 0);
        return;
    }
    if (left_is_register && right_is_memory) {
        int width = destination.width;
        if (source_memory.explicit_size && source_memory.explicit_size != width) as_fail("memory and register sizes do not match");
        if (destination.segment || destination.control) as_fail("invalid memory move register");
        if (width == 16) emit_byte(assembler, 0x66);
        int force = width == 8 && destination.code >= 4 && !destination.high_byte;
        emit_rex(assembler, width, destination.code, 0, source_memory.base, force);
        emit_byte(assembler, width == 8 ? 0x8a : 0x8b);
        emit_memory_tail(assembler, &source_memory, destination.code, width, force, 0);
        return;
    }
    if (left_is_memory && right_is_register) {
        int width = source.width;
        if (destination_memory.explicit_size && destination_memory.explicit_size != width) as_fail("memory and register sizes do not match");
        if (source.segment || source.control) as_fail("invalid memory move register");
        if (width == 16) emit_byte(assembler, 0x66);
        int force = width == 8 && source.code >= 4 && !source.high_byte;
        emit_rex(assembler, width, source.code, 0, destination_memory.base, force);
        emit_byte(assembler, width == 8 ? 0x88 : 0x89);
        emit_memory_tail(assembler, &destination_memory, source.code, width, force, 0);
        return;
    }
    if (left_is_memory) {
        int width = left_size ? left_size : destination_memory.explicit_size;
        if (!width) as_fail("memory immediate requires a size");
        int64_t value;
        int symbol;
        int64_t addend;
        if (as_expression(assembler, right, &value, &symbol, &addend) != 0) as_fail("invalid immediate operand");
        if (width == 8) {
            emit_rex(assembler, 0, 0, 0, destination_memory.base, 0);
            emit_byte(assembler, 0xc6);
            emit_memory_tail(assembler, &destination_memory, 0, width, 0, 0);
            emit_expression_value(assembler, right, 1, 0, 0);
        } else {
            if (width == 16) emit_byte(assembler, 0x66);
            emit_rex(assembler, width, 0, 0, destination_memory.base, 0);
            emit_byte(assembler, 0xc7);
            emit_memory_tail(assembler, &destination_memory, 0, width, 0, 0);
            if (width == 64) emit_expression_value(assembler, right, 4, symbol >= 0 ? 11 : 0, 0);
            else emit_expression_value(assembler, right, width / 8, symbol >= 0 ? 10 : 0, 0);
        }
        return;
    }
    if (left_is_register) {
        if (destination.segment || destination.control || right_is_memory) as_fail("invalid move operands");
        int width = destination.width;
        if (right_size && right_size != width) as_fail("operand sizes do not match");
        if (width == 16) emit_byte(assembler, 0x66);
        int force = width == 8 && destination.code >= 4 && !destination.high_byte;
        emit_rex(assembler, width, 0, 0, 0, force);
        emit_byte(assembler, (unsigned char)((width == 8 ? 0xb0 : 0xb8) + (destination.code & 7)));
        uint32_t relocation_type = width == 64 ? 1 : width == 32 ? 10 : 0;
        emit_expression_value(assembler, right, width / 8, relocation_type, 0);
        return;
    }
    as_fail("unsupported move operands");
}

static void emit_binary(Assembler *assembler, const char *instruction, char *left, char *right) {
    AsRegister destination;
    AsRegister source;
    int destination_register = register_lookup(left, &destination);
    int source_register = register_lookup(right, &source);
    AsMemory destination_memory;
    AsMemory source_memory;
    int destination_is_memory = !destination_register && parse_memory(assembler, left, &destination_memory);
    int source_is_memory = !source_register && parse_memory(assembler, right, &source_memory);
    if (destination_register && source_register) {
        if (destination.width != source.width || destination.segment || source.segment || destination.control || source.control) as_fail("invalid arithmetic registers");
        unsigned char opcode;
        if (strcmp(instruction, "add") == 0) opcode = 0x01;
        else if (strcmp(instruction, "or") == 0) opcode = 0x09;
        else if (strcmp(instruction, "and") == 0) opcode = 0x21;
        else if (strcmp(instruction, "sub") == 0) opcode = 0x29;
        else if (strcmp(instruction, "xor") == 0) opcode = 0x31;
        else if (strcmp(instruction, "cmp") == 0) opcode = 0x39;
        else if (strcmp(instruction, "test") == 0) opcode = 0x85;
        else as_fail("unsupported arithmetic instruction");
        emit_reg_reg(assembler, opcode, source, destination, destination.width, 0);
        return;
    }
    if (destination_register && source_is_memory) {
        if (source_memory.explicit_size && source_memory.explicit_size != destination.width) as_fail("memory and register sizes do not match");
        unsigned char opcode;
        if (strcmp(instruction, "add") == 0) opcode = 0x03;
        else if (strcmp(instruction, "or") == 0) opcode = 0x0b;
        else if (strcmp(instruction, "and") == 0) opcode = 0x23;
        else if (strcmp(instruction, "sub") == 0) opcode = 0x2b;
        else if (strcmp(instruction, "xor") == 0) opcode = 0x33;
        else if (strcmp(instruction, "cmp") == 0) opcode = 0x3b;
        else if (strcmp(instruction, "test") == 0) opcode = 0x85;
        else as_fail("unsupported arithmetic instruction");
        emit_reg_rm(assembler, opcode, destination, &source_memory, destination.width);
        return;
    }
    if (destination_is_memory && source_register) {
        if (destination_memory.explicit_size && destination_memory.explicit_size != source.width) as_fail("memory and register sizes do not match");
        unsigned char opcode;
        if (strcmp(instruction, "add") == 0) opcode = 0x01;
        else if (strcmp(instruction, "or") == 0) opcode = 0x09;
        else if (strcmp(instruction, "and") == 0) opcode = 0x21;
        else if (strcmp(instruction, "sub") == 0) opcode = 0x29;
        else if (strcmp(instruction, "xor") == 0) opcode = 0x31;
        else if (strcmp(instruction, "cmp") == 0) opcode = 0x39;
        else if (strcmp(instruction, "test") == 0) opcode = 0x85;
        else as_fail("unsupported arithmetic instruction");
        if (source.width == 16) emit_byte(assembler, 0x66);
        emit_rex(assembler, source.width, source.code, 0, destination_memory.base, source.width == 8 && source.code >= 4 && !source.high_byte);
        emit_byte(assembler, source.width == 8 ? (unsigned char)(opcode - 1) : opcode);
        emit_memory_tail(assembler, &destination_memory, source.code, source.width, 0, 0);
        return;
    }
    if (destination_register && !source_register && !source_is_memory) {
        int width = destination.width;
        int64_t value;
        int symbol;
        int64_t addend;
        if (as_expression(assembler, right, &value, &symbol, &addend) != 0) as_fail("invalid arithmetic immediate");
        int extension;
        if (strcmp(instruction, "add") == 0) extension = 0;
        else if (strcmp(instruction, "or") == 0) extension = 1;
        else if (strcmp(instruction, "and") == 0) extension = 4;
        else if (strcmp(instruction, "sub") == 0) extension = 5;
        else if (strcmp(instruction, "xor") == 0) extension = 6;
        else if (strcmp(instruction, "cmp") == 0) extension = 7;
        else if (strcmp(instruction, "test") == 0) extension = 0;
        else as_fail("unsupported arithmetic instruction");
        if (strcmp(instruction, "test") == 0) {
            if (width == 16) emit_byte(assembler, 0x66);
            emit_rex(assembler, width, 0, 0, destination.code, 0);
            emit_byte(assembler, width == 8 ? 0xf6 : 0xf7);
            emit_byte(assembler, (unsigned char)(0xc0 | (destination.code & 7)));
            emit_expression_value(assembler, right, width == 8 ? 1 : width == 16 ? 2 : width == 64 ? 4 : 4, width == 64 && symbol >= 0 ? 11 : width == 32 && symbol >= 0 ? 10 : 0, 0);
            return;
        }
        size_t immediate_width = width == 8 ? 1 : width == 16 ? 2 : 4;
        int short_immediate = !symbol && value >= -128 && value <= 127;
        if (width == 16) emit_byte(assembler, 0x66);
        emit_rex(assembler, width, 0, 0, destination.code, 0);
        emit_byte(assembler, width == 8 ? 0x80 : short_immediate ? 0x83 : 0x81);
        emit_byte(assembler, (unsigned char)(0xc0 | (extension << 3) | (destination.code & 7)));
        if (short_immediate) as_emit_integer(assembler, (uint8_t)value, 1);
        else emit_expression_value(assembler, right, immediate_width, symbol >= 0 ? 11 : 0, 0);
        return;
    }
    as_fail("unsupported arithmetic operands");
}

static void emit_unary(Assembler *assembler, const char *instruction, char *operand) {
    AsRegister reg;
    AsMemory memory;
    if (register_lookup(operand, &reg)) {
        if (reg.segment || reg.control) as_fail("invalid unary register");
        int extension = strcmp(instruction, "not") == 0 ? 2 : strcmp(instruction, "neg") == 0 ? 3 : strcmp(instruction, "mul") == 0 ? 4 : strcmp(instruction, "imul") == 0 ? 5 : 7;
        if (reg.width == 16) emit_byte(assembler, 0x66);
        emit_rex(assembler, reg.width, 0, 0, reg.code, reg.width == 8 && reg.code >= 4 && !reg.high_byte);
        emit_byte(assembler, reg.width == 8 ? 0xf6 : 0xf7);
        emit_byte(assembler, (unsigned char)(0xc0 | (extension << 3) | (reg.code & 7)));
        return;
    }
    if (parse_memory(assembler, operand, &memory)) {
        int width = memory.explicit_size;
        if (!width) as_fail("memory unary operand requires a size");
        int extension = strcmp(instruction, "not") == 0 ? 2 : strcmp(instruction, "neg") == 0 ? 3 : strcmp(instruction, "inc") == 0 ? 0 : strcmp(instruction, "dec") == 0 ? 1 : 7;
        if (width == 16) emit_byte(assembler, 0x66);
        emit_rex(assembler, width, 0, 0, memory.base, 0);
        emit_byte(assembler, width == 8 ? 0xf6 : 0xf7);
        emit_memory_tail(assembler, &memory, extension, width, 0, 0);
        return;
    }
    as_fail("invalid unary operand");
}

static void emit_push_pop(Assembler *assembler, const char *instruction, char *operand) {
    AsRegister reg;
    if (register_lookup(operand, &reg)) {
        if (reg.width == 16) emit_byte(assembler, 0x66);
        if (reg.code >= 8) emit_byte(assembler, 0x41);
        emit_byte(assembler, (unsigned char)((strcmp(instruction, "push") == 0 ? 0x50 : 0x58) + (reg.code & 7)));
        return;
    }
    if (strcmp(instruction, "pop") == 0) as_fail("pop requires a register");
    int64_t value;
    int symbol;
    int64_t addend;
    if (as_expression(assembler, operand, &value, &symbol, &addend) != 0) as_fail("invalid push operand");
    if (symbol < 0 && value >= -128 && value <= 127) {
        emit_byte(assembler, 0x6a);
        emit_byte(assembler, (unsigned char)value);
    } else {
        emit_byte(assembler, 0x68);
        if (symbol >= 0) emit_relocation(assembler, symbol, 11, addend, 4);
        else as_emit_integer(assembler, (uint32_t)value, 4);
    }
}

static void emit_shift(Assembler *assembler, const char *instruction, char *left, char *right) {
    AsRegister reg;
    if (!register_lookup(left, &reg) || reg.segment || reg.control) as_fail("shift requires a register destination");
    int extension = strcmp(instruction, "shl") == 0 || strcmp(instruction, "sal") == 0 ? 4 : strcmp(instruction, "shr") == 0 ? 5 : 7;
    if (strcmp(right, "cl") == 0) {
        if (reg.width == 16) emit_byte(assembler, 0x66);
        emit_rex(assembler, reg.width, 0, 0, reg.code, 0);
        emit_byte(assembler, reg.width == 8 ? 0xd2 : 0xd3);
        emit_byte(assembler, (unsigned char)(0xc0 | (extension << 3) | (reg.code & 7)));
    } else {
        int64_t value;
        int symbol;
        int64_t addend;
        if (as_expression(assembler, right, &value, &symbol, &addend) != 0 || symbol >= 0 || value < 0 || value > 255) as_fail("invalid shift count");
        if (reg.width == 16) emit_byte(assembler, 0x66);
        emit_rex(assembler, reg.width, 0, 0, reg.code, 0);
        emit_byte(assembler, reg.width == 8 ? 0xc0 : 0xc1);
        emit_byte(assembler, (unsigned char)(0xc0 | (extension << 3) | (reg.code & 7)));
        emit_byte(assembler, (unsigned char)value);
    }
}

static void emit_lea(Assembler *assembler, char *left, char *right) {
    AsRegister destination;
    AsMemory source;
    if (!register_lookup(left, &destination) || !parse_memory(assembler, right, &source) || destination.width < 32 || destination.segment || destination.control) as_fail("invalid lea operands");
    if (source.explicit_size) as_fail("lea does not accept a memory size qualifier");
    emit_reg_rm(assembler, 0x8d, destination, &source, destination.width);
}

void as_encode_instruction(Assembler *assembler, char *line) {
    char *cursor = trim_operand(line);
    char *instruction = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (*cursor) *cursor++ = 0;
    char *operands = trim_operand(cursor);
    char *left;
    char *right;
    int count = split_operands(operands, &left, &right);
    if (strcmp(instruction, "ret") == 0) { emit_byte(assembler, 0xc3); return; }
    if (strcmp(instruction, "retf") == 0 || strcmp(instruction, "retfq") == 0) { emit_byte(assembler, 0xcb); return; }
    if (strcmp(instruction, "iretq") == 0) { emit_byte(assembler, 0x48); emit_byte(assembler, 0xcf); return; }
    if (strcmp(instruction, "nop") == 0) { emit_byte(assembler, 0x90); return; }
    if (strcmp(instruction, "cli") == 0) { emit_byte(assembler, 0xfa); return; }
    if (strcmp(instruction, "sti") == 0) { emit_byte(assembler, 0xfb); return; }
    if (strcmp(instruction, "hlt") == 0) { emit_byte(assembler, 0xf4); return; }
    if (strcmp(instruction, "cld") == 0) { emit_byte(assembler, 0xfc); return; }
    if (strcmp(instruction, "std") == 0) { emit_byte(assembler, 0xfd); return; }
    if (strcmp(instruction, "syscall") == 0) { emit_byte(assembler, 0x0f); emit_byte(assembler, 0x05); return; }
    if (strcmp(instruction, "rdmsr") == 0) { emit_byte(assembler, 0x0f); emit_byte(assembler, 0x32); return; }
    if (strcmp(instruction, "wrmsr") == 0) { emit_byte(assembler, 0x0f); emit_byte(assembler, 0x30); return; }
    if (strcmp(instruction, "leave") == 0) { emit_byte(assembler, 0xc9); return; }
    if (strcmp(instruction, "int3") == 0) { emit_byte(assembler, 0xcc); return; }
    if (strcmp(instruction, "int") == 0) {
        int64_t value;
        int symbol;
        int64_t addend;
        if (count != 1 || as_expression(assembler, left, &value, &symbol, &addend) != 0 || symbol >= 0 || value < 0 || value > 255) as_fail("invalid interrupt vector");
        emit_byte(assembler, 0xcd); emit_byte(assembler, (unsigned char)value); return;
    }
    if (strcmp(instruction, "lgdt") == 0 || strcmp(instruction, "lidt") == 0) {
        AsMemory memory;
        if (count != 1 || !parse_memory(assembler, left, &memory)) as_fail("invalid descriptor table operand");
        emit_byte(assembler, 0x0f); emit_byte(assembler, 0x01);
        emit_memory_tail(assembler, &memory, strcmp(instruction, "lgdt") == 0 ? 2 : 3, 0, 0, 0);
        return;
    }
    if (strcmp(instruction, "call") == 0 || strcmp(instruction, "jmp") == 0 || condition_code(instruction) >= 0 || strcmp(instruction, "loop") == 0) {
        if (count != 1) as_fail("branch requires one target");
        if (strcmp(instruction, "jmp") == 0 && strchr(left, ':')) emit_far_jump(assembler, left);
        else emit_branch(assembler, instruction, left);
        return;
    }
    if (strcmp(instruction, "push") == 0 || strcmp(instruction, "pop") == 0) {
        if (count != 1) as_fail("stack instruction requires one operand");
        emit_push_pop(assembler, instruction, left);
        return;
    }
    if (strcmp(instruction, "mov") == 0) {
        if (count != 2) as_fail("mov requires two operands");
        emit_move(assembler, left, right);
        return;
    }
    if (strcmp(instruction, "lea") == 0) {
        if (count != 2) as_fail("lea requires two operands");
        emit_lea(assembler, left, right);
        return;
    }
    if (strcmp(instruction, "shl") == 0 || strcmp(instruction, "sal") == 0 || strcmp(instruction, "shr") == 0 || strcmp(instruction, "sar") == 0) {
        if (count != 2) as_fail("shift requires two operands");
        emit_shift(assembler, instruction, left, right);
        return;
    }
    if (strcmp(instruction, "not") == 0 || strcmp(instruction, "neg") == 0 || strcmp(instruction, "inc") == 0 || strcmp(instruction, "dec") == 0 || strcmp(instruction, "mul") == 0 || strcmp(instruction, "imul") == 0 || strcmp(instruction, "idiv") == 0 || strcmp(instruction, "div") == 0) {
        if (count != 1) as_fail("unary instruction requires one operand");
        emit_unary(assembler, instruction, left);
        return;
    }
    if (strcmp(instruction, "add") == 0 || strcmp(instruction, "or") == 0 || strcmp(instruction, "and") == 0 || strcmp(instruction, "sub") == 0 || strcmp(instruction, "xor") == 0 || strcmp(instruction, "cmp") == 0 || strcmp(instruction, "test") == 0) {
        if (count != 2) as_fail("arithmetic instruction requires two operands");
        emit_binary(assembler, instruction, left, right);
        return;
    }
    as_fail("unsupported instruction");
}
