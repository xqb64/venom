#include <stdint.h>
#include <stdio.h>

#include "disassembler.h"
#include "dynarray.h"

typedef struct {
  char *opcode;
  size_t operands;
} DisassembleHandler;

static DisassembleHandler disassemble_handler[] = {
    [OP_PRINT] = {.opcode = "OP_PRINT", .operands = 0},
    [OP_ADD] = {.opcode = "OP_ADD", .operands = 0},
    [OP_SUB] = {.opcode = "OP_SUB", .operands = 0},
    [OP_MUL] = {.opcode = "OP_MUL", .operands = 0},
    [OP_DIV] = {.opcode = "OP_DIV", .operands = 0},
    [OP_MOD] = {.opcode = "OP_MOD", .operands = 0},
    [OP_EQ] = {.opcode = "OP_EQ", .operands = 0},
    [OP_GT] = {.opcode = "OP_GT", .operands = 0},
    [OP_LT] = {.opcode = "OP_LT", .operands = 0},
    [OP_BITAND] = {.opcode = "OP_BITAND", .operands = 0},
    [OP_BITOR] = {.opcode = "OP_BITOR", .operands = 0},
    [OP_BITXOR] = {.opcode = "OP_BITXOR", .operands = 0},
    [OP_BITNOT] = {.opcode = "OP_BITNOT", .operands = 0},
    [OP_BITSHL] = {.opcode = "OP_BITSHL", .operands = 0},
    [OP_BITSHR] = {.opcode = "OP_BITSHR", .operands = 0},
    [OP_NOT] = {.opcode = "OP_NOT", .operands = 0},
    [OP_NEG] = {.opcode = "OP_NEG", .operands = 0},
    [OP_TRUE] = {.opcode = "OP_TRUE", .operands = 0},
    [OP_NULL] = {.opcode = "OP_NULL", .operands = 0},
    [OP_CONST] = {.opcode = "OP_CONST", .operands = 4},
    [OP_STR] = {.opcode = "OP_STR", .operands = 4},
    [OP_JZ] = {.opcode = "OP_JZ", .operands = 2},
    [OP_JMP] = {.opcode = "OP_JMP", .operands = 2},
    [OP_SET_GLOBAL] = {.opcode = "OP_SET_GLOBAL", .operands = 4},
    [OP_GET_GLOBAL] = {.opcode = "OP_GET_GLOBAL", .operands = 4},
    [OP_GET_GLOBAL_PTR] = {.opcode = "OP_GET_GLOBAL_PTR", .operands = 4},
    [OP_DEEPSET] = {.opcode = "OP_DEEPSET", .operands = 4},
    [OP_DEEPGET] = {.opcode = "OP_DEEPGET", .operands = 4},
    [OP_DEEPGET_PTR] = {.opcode = "OP_DEEPGET_PTR", .operands = 4},
    [OP_SETATTR] = {.opcode = "OP_SETATTR", .operands = 4},
    [OP_GETATTR] = {.opcode = "OP_GETATTR", .operands = 4},
    [OP_GETATTR_PTR] = {.opcode = "OP_GETATTR_PTR", .operands = 4},
    [OP_STRUCT] = {.opcode = "OP_STRUCT", .operands = 4},
    [OP_RET] = {.opcode = "OP_RET", .operands = 0},
    [OP_POP] = {.opcode = "OP_POP", .operands = 0},
    [OP_DUP] = {.opcode = "OP_DUP", .operands = 0},
    [OP_DEREF] = {.opcode = "OP_DEREF", .operands = 0},
    [OP_DEREFSET] = {.opcode = "OP_DEREFSET", .operands = 0},
    [OP_CALL] = {.opcode = "OP_CALL", .operands = 4},
};

void disassemble(BytecodeChunk *chunk) {
#define READ_UINT8() (*++ip)
#define READ_INT16() (ip += 2, (int16_t)((ip[-1] << 8) | ip[0]))

#define READ_UINT32()                                                          \
  (ip += 4, (uint32_t)((ip[-3] << 24) | (ip[-2] << 16) | (ip[-1] << 8) | ip[0]))

  for (uint8_t *ip = chunk->code.data;
       ip < &chunk->code.data[chunk->code.count]; /* ip < addr of just beyond
                                                     the last instruction */
       ip++) {
    printf("%ld: ", ip - chunk->code.data);
    printf("%s", disassemble_handler[*ip].opcode);
    switch (disassemble_handler[*ip].operands) {
    case 0:
      break;
    case 2: {
      printf(" + 2-byte offset: %d", READ_INT16());
      break;
    }
    case 4: {
      switch (*ip) {
      case OP_CONST: {
        uint32_t const_idx = READ_UINT32();
        printf(" (value: %.16g)", chunk->cp.data[const_idx]);
        break;
      }
      case OP_STR: {
        uint32_t str_idx = READ_UINT32();
        printf(" (value: %s)", chunk->sp.data[str_idx]);
        break;
      }
      case OP_DEEPGET:
      case OP_DEEPGET_PTR:
      case OP_DEEPSET: {
        uint32_t idx = READ_UINT32();
        printf(" (index: %d)", idx);
        break;
      }
      case OP_GET_GLOBAL:
      case OP_GET_GLOBAL_PTR:
      case OP_SET_GLOBAL: {
        uint32_t name_idx = READ_UINT32();
        printf(" (name: %s)", chunk->sp.data[name_idx]);
        break;
      }
      case OP_GETATTR:
      case OP_SETATTR: {
        uint32_t property_name_idx = READ_UINT32();
        printf(" (property: %s)", chunk->sp.data[property_name_idx]);
        break;
      }
      case OP_STRUCT: {
        uint32_t name_idx = READ_UINT32();
        printf(" (name: %s)", chunk->sp.data[name_idx]);
        break;
      }
      case OP_CALL: {
        uint32_t argcount = READ_UINT32();
        printf(" (argcount: %d)", argcount);
        break;
      }
      default:
        break;
      }
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
