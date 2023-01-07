#include <stdio.h>
#include "disassembler.h"

typedef struct {
  char *opcode;
  size_t operands;
} DisassembleHandler;

DisassembleHandler disassemble_handler[] = {
    [OP_PRINT] = { .opcode = "OP_PRINT", .operands = 0 },
    [OP_CONST] = { .opcode = "OP_CONST", .operands = 1 },
    [OP_GET_GLOBAL] = { .opcode = "OP_GET_GLOBAL", .operands = 1 },
    [OP_SET_GLOBAL] = { .opcode = "OP_SET_GLOBAL", .operands = 1 },
    [OP_STR] = { .opcode = "OP_STR", .operands = 1 },
    [OP_DEEPGET] = { .opcode = "OP_DEEPGET", .operands = 1 },
    [OP_DEEPSET] = { .opcode = "OP_DEEPSET", .operands = 1 },
    [OP_GETATTR] = { .opcode = "OP_GETATTR", .operands = 1 },
    [OP_SETATTR] = { .opcode = "OP_SETATTR", .operands = 1 },
    [OP_ADD] = { .opcode = "OP_ADD", .operands = 0 },
    [OP_SUB] = { .opcode = "OP_SUB", .operands = 0 },
    [OP_MUL] = { .opcode = "OP_MUL", .operands = 0 },
    [OP_DIV] = { .opcode = "OP_DIV", .operands = 0 },
    [OP_MOD] = { .opcode = "OP_MOD", .operands = 0 },
    [OP_GT] = { .opcode = "OP_GT", .operands = 0 },
    [OP_LT] = { .opcode = "OP_LT", .operands = 0 },
    [OP_EQ] = { .opcode = "OP_EQ", .operands = 0 },
    [OP_JZ] = { .opcode = "OP_JZ", .operands = 2 },
    [OP_JMP] = { .opcode = "OP_JMP", .operands = 2 },
    [OP_NEG] = { .opcode = "OP_NEG", .operands = 0 },
    [OP_NOT] = { .opcode = "OP_NOT", .operands = 0 },
    [OP_RET] = { .opcode = "OP_RET", .operands = 0 },
    [OP_TRUE] = { .opcode = "OP_TRUE", .operands = 0 },
    [OP_NULL] = { .opcode = "OP_NULL", .operands = 0 },
    [OP_STRUCT] = { .opcode = "OP_STRUCT", .operands = 2 },
    [OP_IP] = { .opcode = "OP_IP", .operands = 2 },
    [OP_INC_FPCOUNT] = { .opcode = "OP_INC_FPCOUNT", .operands = 0 },
    [OP_POP] = { .opcode = "OP_POP", .operands = 0 },
};

void disassemble(BytecodeChunk *chunk) {
#define READ_UINT8() (*++ip)

#define READ_INT16() \
    /* ip points to one of the jump instructions and there \
     * is a 2-byte operand (offset) that comes after the jump \
     * instruction. We want to increment the ip so it points \
     * to the last of the two operands, and construct a 16-bit \
     * offset from the two bytes. Then ip is incremented in \
     * the loop again so it points to the next instruction \
     * (as opposed to pointing somewhere in the middle). */ \
    (ip += 2, \
    (int16_t)((ip[-1] << 8) | ip[0]))
    
    for (
        uint8_t *ip = chunk->code.data;    
        ip < &chunk->code.data[chunk->code.count];  /* ip < addr of just beyond the last instruction */
        ip++
    ) {
        printf("%ld: ", ip - chunk->code.data);
        printf("%s", disassemble_handler[*ip].opcode);
        switch (disassemble_handler[*ip].operands) {
            case 0: break;
            case 1: {
                switch (*ip) {
                    case OP_CONST: {
                        uint8_t const_index = READ_UINT8();
                        printf(" (value: %f)", chunk->cp[const_index]);
                        break;
                    }
                    case OP_STR: {
                        uint8_t str_index = READ_UINT8();
                        printf(" (value: %s)", chunk->sp[str_index]);
                        break;
                    }
                    default: {
                        printf(" (index: %d)", READ_UINT8());
                        break;
                    } 
                }
                break;
            }
            case 2: {
                switch (*ip) {
                    case OP_STRUCT: {
                        uint8_t name_index = READ_UINT8();
                        uint8_t propertycount = READ_UINT8();
                        printf(" (name: %s, propertycount: %d)", chunk->sp[name_index], propertycount);
                        break;
                    }
                    default: {
                        printf(" (offset: %d)", READ_INT16());
                        break;
                    }
                }
                break;
            }
            default: break;
        }
        printf("\n");
    }
}
