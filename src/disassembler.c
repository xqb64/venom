#include "disassembler.h"

#include <stdint.h>
#include <stdio.h>

#include "compiler.h"
#include "dynarray.h"

DisassembleHandler disassemble_handler[] = {
    [OP_PRINT] = {.opcode = "OP_PRINT"},
    [OP_ADD] = {.opcode = "OP_ADD"},
    [OP_SUB] = {.opcode = "OP_SUB"},
    [OP_MUL] = {.opcode = "OP_MUL"},
    [OP_DIV] = {.opcode = "OP_DIV"},
    [OP_MOD] = {.opcode = "OP_MOD"},
    [OP_EQ] = {.opcode = "OP_EQ"},
    [OP_GT] = {.opcode = "OP_GT"},
    [OP_LT] = {.opcode = "OP_LT"},
    [OP_BITAND] = {.opcode = "OP_BITAND"},
    [OP_BITOR] = {.opcode = "OP_BITOR"},
    [OP_BITXOR] = {.opcode = "OP_BITXOR"},
    [OP_BITNOT] = {.opcode = "OP_BITNOT"},
    [OP_BITSHL] = {.opcode = "OP_BITSHL"},
    [OP_BITSHR] = {.opcode = "OP_BITSHR"},
    [OP_NOT] = {.opcode = "OP_NOT"},
    [OP_NEG] = {.opcode = "OP_NEG"},
    [OP_TRUE] = {.opcode = "OP_TRUE"},
    [OP_NULL] = {.opcode = "OP_NULL"},
    [OP_CONST] = {.opcode = "OP_CONST"},
    [OP_STR] = {.opcode = "OP_STR"},
    [OP_STRCAT] = {.opcode = "OP_STRCAT"},
    [OP_JZ] = {.opcode = "OP_JZ"},
    [OP_JMP] = {.opcode = "OP_JMP"},
    [OP_SET_GLOBAL] = {.opcode = "OP_SET_GLOBAL"},
    [OP_GET_GLOBAL] = {.opcode = "OP_GET_GLOBAL"},
    [OP_GET_GLOBAL_PTR] = {.opcode = "OP_GET_GLOBAL_PTR"},
    [OP_DEEPSET] = {.opcode = "OP_DEEPSET"},
    [OP_DEEPGET] = {.opcode = "OP_DEEPGET"},
    [OP_DEEPGET_PTR] = {.opcode = "OP_DEEPGET_PTR"},
    [OP_SETATTR] = {.opcode = "OP_SETATTR"},
    [OP_GETATTR] = {.opcode = "OP_GETATTR"},
    [OP_GETATTR_PTR] = {.opcode = "OP_GETATTR_PTR"},
    [OP_STRUCT] = {.opcode = "OP_STRUCT"},
    [OP_RET] = {.opcode = "OP_RET"},
    [OP_POP] = {.opcode = "OP_POP"},
    [OP_DEREF] = {.opcode = "OP_DEREF"},
    [OP_DEREFSET] = {.opcode = "OP_DEREFSET"},
    [OP_CALL] = {.opcode = "OP_CALL"},
    [OP_STRUCT_BLUEPRINT] = {.opcode = "OP_STRUCT_BLUEPRINT"},
    [OP_GET_UPVALUE] = {.opcode = "OP_GET_UPVALUE"},
    [OP_GET_UPVALUE_PTR] = {.opcode = "OP_GET_UPVALUE_PTR"},
    [OP_SET_UPVALUE] = {.opcode = "OP_SET_UPVALUE"},
    [OP_CLOSURE] = {.opcode = "OP_CLOSURE"},
    [OP_IMPL] = {.opcode = "OP_IMPL"},
    [OP_YIELD] = {.opcode = "OP_YIELD"},
    [OP_RESUME] = {.opcode = "OP_RESUME"},
    [OP_HLT] = {.opcode = "OP_HLT"},
};

void disassemble(Bytecode *code)
{
#define READ_UINT8() (*++ip)
#define READ_INT16() (ip += 2, (int16_t) ((ip[-1] << 8) | ip[0]))

#define READ_UINT32() \
    (ip += 4, (uint32_t) ((ip[-3] << 24) | (ip[-2] << 16) | (ip[-1] << 8) | ip[0]))

#define READ_DOUBLE()                                                                              \
    (ip += 8,                                                                                      \
     (double) (((uint64_t) ip[-7] << 56) | ((uint64_t) ip[-6] << 48) | ((uint64_t) ip[-5] << 40) | \
               ((uint64_t) ip[-4] << 32) | ((uint64_t) ip[-3] << 24) | ((uint64_t) ip[-2] << 16) | \
               ((uint64_t) ip[-1] << 8) | (uint64_t) ip[0]))

    for (uint8_t *ip = code->code.data; ip < &code->code.data[code->code.count]; ip++)
    {
        printf("%ld: ", ip - code->code.data);
        printf("%s", disassemble_handler[*ip].opcode);

        switch (*ip)
        {
            case OP_CONST: {
                union {
                    double d;
                    uint64_t raw;
                } num;
                num.raw = READ_DOUBLE();
                printf(" (%.16g)", num.d);
                break;
            }
            case OP_CLOSURE: {
                uint32_t name_idx, paramcount, location, upvalue_count;

                name_idx = READ_UINT32();
                paramcount = READ_UINT32();
                location = READ_UINT32();
                upvalue_count = READ_UINT32();

                printf(" (name: %s, paramcount: %d, location: %d, upvalue_count: %d)",
                       code->sp.data[name_idx], paramcount, location, upvalue_count);

                for (size_t i = 0; i < upvalue_count; i++)
                    READ_UINT32();

                break;
            }
            case OP_JMP:
            case OP_JZ: {
                int16_t offset = READ_INT16();
                printf(" (offset: %d)", offset);
                break;
            }
            case OP_DEEPGET:
            case OP_DEEPGET_PTR:
            case OP_DEEPSET: {
                uint32_t idx;

                idx = READ_UINT32();

                printf(" (idx: %d)", idx);
                break;
            }
            case OP_CALL: {
                uint32_t argcount, callee_idx;

                argcount = READ_UINT32();
                callee_idx = READ_UINT32();

                printf(" (callee: %s, argcount: %d)", code->sp.data[callee_idx], argcount);
                break;
            }
            case OP_GET_GLOBAL:
            case OP_SET_GLOBAL: {
                uint32_t name_idx;

                name_idx = READ_UINT32();

                printf(" (name: %s)", code->sp.data[name_idx]);
                break;
            }
            case OP_GET_UPVALUE:
            case OP_GET_UPVALUE_PTR:
            case OP_SET_UPVALUE: {
                uint32_t idx;

                idx = READ_UINT32();

                printf(" (idx: %d)", idx);
                break;
            }
            case OP_RET: {
                uint32_t popcount;

                popcount = READ_UINT32();

                printf(" (popcount: %d)", popcount);
                break;
            }
            default:
                break;
        }

        printf("\n");
    }
#undef READ_UINT8
#undef READ_INT16
#undef READ_UINT32
}
