#include "disassembler.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "compiler.h"

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
    [OP_NOT] = {.opcode = "OP_NOT"},
    [OP_NEG] = {.opcode = "OP_NEG"},
    [OP_TRUE] = {.opcode = "OP_TRUE"},
    [OP_NULL] = {.opcode = "OP_NULL"},
    [OP_CONST] = {.opcode = "OP_CONST"},
    [OP_STR] = {.opcode = "OP_STR"},
    [OP_JMP] = {.opcode = "OP_JMP"},
    [OP_JZ] = {.opcode = "OP_JZ"},
    [OP_BITAND] = {.opcode = "OP_BITAND"},
    [OP_BITOR] = {.opcode = "OP_BITOR"},
    [OP_BITXOR] = {.opcode = "OP_BITXOR"},
    [OP_BITNOT] = {.opcode = "OP_BITNOT"},
    [OP_BITSHL] = {.opcode = "OP_BITSHL"},
    [OP_BITSHR] = {.opcode = "OP_BITSHR"},
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
    [OP_STRUCT_BLUEPRINT] = {.opcode = "OP_STRUCT_BLUEPRINT"},
    [OP_CLOSURE] = {.opcode = "OP_CLOSURE"},
    [OP_CALL] = {.opcode = "OP_CALL"},
    [OP_CALL_METHOD] = {.opcode = "OP_CALL_METHOD"},
    [OP_RET] = {.opcode = "OP_RET"},
    [OP_POP] = {.opcode = "OP_POP"},
    [OP_DEREF] = {.opcode = "OP_DEREF"},
    [OP_DEREFSET] = {.opcode = "OP_DEREFSET"},
    [OP_STRCAT] = {.opcode = "OP_STRCAT"},
    [OP_ARRAY] = {.opcode = "OP_ARRAY"},
    [OP_ARRAYSET] = {.opcode = "OP_ARRAYSET"},
    [OP_SUBSCRIPT] = {.opcode = "OP_SUBSCRIPT"},
    [OP_GET_UPVALUE] = {.opcode = "OP_GET_UPVALUE"},
    [OP_GET_UPVALUE_PTR] = {.opcode = "OP_GET_UPVALUE_PTR"},
    [OP_SET_UPVALUE] = {.opcode = "OP_SET_UPVALUE"},
    [OP_CLOSE_UPVALUE] = {.opcode = "OP_CLOSE_UPVALUE"},
    [OP_IMPL] = {.opcode = "OP_IMPL"},
    [OP_MKGEN] = {.opcode = "OP_MKGEN"},
    [OP_YIELD] = {.opcode = "OP_YIELD"},
    [OP_RESUME] = {.opcode = "OP_RESUME"},
    [OP_SEND] = {.opcode = "OP_SEND"},
    [OP_AWAIT] = {.opcode = "OP_AWAIT"},
    [OP_SPAWN] = {.opcode = "OP_SPAWN"},
    [OP_RUN] = {.opcode = "OP_RUN"},
    [OP_SLEEP] = {.opcode = "OP_SLEEP"},
    [OP_DONE] = {.opcode = "OP_DONE"},
    [OP_RESULT] = {.opcode = "OP_RESULT"},
    [OP_LEN] = {.opcode = "OP_LEN"},
    [OP_HASATTR] = {.opcode = "OP_HASATTR"},
    [OP_ASSERT] = {.opcode = "OP_ASSERT"},
    [OP_HLT] = {.opcode = "OP_HLT"},
};

DisassembleResult disassemble(Bytecode *code)
{
#define STRING_AT(idx)                                                     \
  (((idx) < code->sp.count && code->sp.data[(idx)] != NULL)                \
       ? code->sp.data[(idx)]                                              \
       : "<invalid string index>")

#define FAIL(message)                                                       \
  do {                                                                      \
    clock_gettime(CLOCK_MONOTONIC, &end);                                   \
    result.time =                                                           \
        (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;   \
    result.is_ok = false;                                                   \
    result.errcode = -1;                                                    \
    result.msg = strdup((message));                                         \
    return result;                                                          \
  } while (0)

#define REQUIRE_OPERAND_BYTES(n)                                            \
  do {                                                                      \
    if ((size_t) (end_ip - ip - 1) < (size_t) (n)) {                         \
      FAIL("Disassembling failed: truncated instruction stream.");           \
    }                                                                       \
  } while (0)

#define READ_UINT8(out)                                                     \
  do {                                                                      \
    REQUIRE_OPERAND_BYTES(1);                                               \
    (out) = *++ip;                                                          \
  } while (0)

#define READ_INT16(out)                                                     \
  do {                                                                      \
    REQUIRE_OPERAND_BYTES(2);                                               \
    ip += 2;                                                                \
    (out) = (int16_t) (((uint16_t) ip[-1] << 8) | (uint16_t) ip[0]);         \
  } while (0)

#define READ_DOUBLE_RAW(out)                                                \
  do {                                                                      \
    REQUIRE_OPERAND_BYTES(8);                                               \
    ip += 8;                                                                \
    (out) = (((uint64_t) ip[-7] << 56) | ((uint64_t) ip[-6] << 48) |         \
             ((uint64_t) ip[-5] << 40) | ((uint64_t) ip[-4] << 32) |         \
             ((uint64_t) ip[-3] << 24) | ((uint64_t) ip[-2] << 16) |         \
             ((uint64_t) ip[-1] << 8) | (uint64_t) ip[0]);                  \
  } while (0)

  DisassembleResult result = {
      .is_ok = true,
      .errcode = 0,
      .msg = NULL,
      .time = 0.0,
  };

  struct timespec start, end;

  clock_gettime(CLOCK_MONOTONIC, &start);

  uint8_t *end_ip = &code->code.data[code->code.count];

  for (uint8_t *ip = code->code.data; ip < end_ip; ip++) {
    uint8_t opcode = *ip;

    if (opcode >= sizeof(disassemble_handler) / sizeof(disassemble_handler[0]) ||
        disassemble_handler[opcode].opcode == NULL) {
      FAIL("Disassembling failed: unknown opcode.");
    }

    printf("%ld: ", (long) (ip - code->code.data));
    printf("%s", disassemble_handler[opcode].opcode);

    switch (opcode) {
      case OP_CONST: {
        union {
          double d;
          uint64_t raw;
        } num;

        READ_DOUBLE_RAW(num.raw);

        printf(" (%.16g)", num.d);
        break;
      }

      case OP_STR: {
        uint8_t idx;

        READ_UINT8(idx);

        printf(" (idx: %u, value: %s)", (unsigned) idx, STRING_AT(idx));
        break;
      }

      case OP_JMP:
      case OP_JZ: {
        int16_t offset;

        READ_INT16(offset);

        printf(" (offset: %d)", offset);
        break;
      }

      case OP_SET_GLOBAL:
      case OP_GET_GLOBAL:
      case OP_GET_GLOBAL_PTR: {
        uint8_t name_idx;

        READ_UINT8(name_idx);

        printf(" (name: %s)", STRING_AT(name_idx));
        break;
      }

      case OP_DEEPSET:
      case OP_DEEPGET:
      case OP_DEEPGET_PTR: {
        uint8_t idx;

        READ_UINT8(idx);

        printf(" (idx: %u)", (unsigned) idx);
        break;
      }

      case OP_SETATTR:
      case OP_GETATTR:
      case OP_GETATTR_PTR: {
        uint8_t property_name_idx;

        READ_UINT8(property_name_idx);

        printf(" (property: %s)", STRING_AT(property_name_idx));
        break;
      }

      case OP_STRUCT: {
        uint8_t struct_name_idx;

        READ_UINT8(struct_name_idx);

        printf(" (name: %s)", STRING_AT(struct_name_idx));
        break;
      }

      case OP_STRUCT_BLUEPRINT: {
        uint8_t name_idx;
        uint8_t propcount;

        READ_UINT8(name_idx);
        READ_UINT8(propcount);

        printf(" (name: %s, propcount: %u",
               STRING_AT(name_idx),
               (unsigned) propcount);

        if (propcount > 0) {
          printf(", properties: [");
        }

        for (uint8_t i = 0; i < propcount; i++) {
          uint8_t property_name_idx;
          uint8_t property_idx;

          READ_UINT8(property_name_idx);
          READ_UINT8(property_idx);

          printf("%s{name: %s, idx: %u}",
                 i == 0 ? "" : ", ",
                 STRING_AT(property_name_idx),
                 (unsigned) property_idx);
        }

        if (propcount > 0) {
          printf("]");
        }

        printf(")");
        break;
      }

      case OP_IMPL: {
        uint8_t blueprint_name_idx;
        uint8_t method_count;

        READ_UINT8(blueprint_name_idx);
        READ_UINT8(method_count);

        printf(" (blueprint: %s, method_count: %u",
               STRING_AT(blueprint_name_idx),
               (unsigned) method_count);

        if (method_count > 0) {
          printf(", methods: [");
        }

        for (uint8_t i = 0; i < method_count; i++) {
          uint8_t method_name_idx;
          uint8_t paramcount;
          uint8_t location;

          READ_UINT8(method_name_idx);
          READ_UINT8(paramcount);
          READ_UINT8(location);

          printf("%s{name: %s, paramcount: %u, location: %u}",
                 i == 0 ? "" : ", ",
                 STRING_AT(method_name_idx),
                 (unsigned) paramcount,
                 (unsigned) location);
        }

        if (method_count > 0) {
          printf("]");
        }

        printf(")");
        break;
      }

      case OP_CLOSURE: {
        uint8_t name_idx;
        uint8_t paramcount;
        uint8_t location;
        uint8_t upvalue_count;

        READ_UINT8(name_idx);
        READ_UINT8(paramcount);
        READ_UINT8(location);
        READ_UINT8(upvalue_count);

        printf(" (name: %s, paramcount: %u, location: %u, upvalue_count: %u",
               STRING_AT(name_idx),
               (unsigned) paramcount,
               (unsigned) location,
               (unsigned) upvalue_count);

        if (upvalue_count > 0) {
          printf(", upvalues: [");
        }

        for (uint8_t i = 0; i < upvalue_count; i++) {
          uint8_t idx;

          READ_UINT8(idx);

          printf("%s%u", i == 0 ? "" : ", ", (unsigned) idx);
        }

        if (upvalue_count > 0) {
          printf("]");
        }

        printf(")");
        break;
      }

      case OP_CALL: {
        uint8_t argcount;

        READ_UINT8(argcount);

        printf(" (argcount: %u)", (unsigned) argcount);
        break;
      }

      case OP_CALL_METHOD: {
        uint8_t method_name_idx;
        uint8_t argcount;

        READ_UINT8(method_name_idx);
        READ_UINT8(argcount);

        printf(" (method: %s, argcount: %u)",
               STRING_AT(method_name_idx),
               (unsigned) argcount);
        break;
      }

      case OP_ARRAY: {
        uint8_t count;

        READ_UINT8(count);

        printf(" (count: %u)", (unsigned) count);
        break;
      }

      case OP_GET_UPVALUE:
      case OP_GET_UPVALUE_PTR:
      case OP_SET_UPVALUE: {
        uint8_t idx;

        READ_UINT8(idx);

        printf(" (idx: %u)", (unsigned) idx);
        break;
      }

      case OP_PRINT:
      case OP_ADD:
      case OP_SUB:
      case OP_MUL:
      case OP_DIV:
      case OP_MOD:
      case OP_EQ:
      case OP_GT:
      case OP_LT:
      case OP_NOT:
      case OP_NEG:
      case OP_TRUE:
      case OP_NULL:
      case OP_BITAND:
      case OP_BITOR:
      case OP_BITXOR:
      case OP_BITNOT:
      case OP_BITSHL:
      case OP_BITSHR:
      case OP_RET:
      case OP_POP:
      case OP_DEREF:
      case OP_DEREFSET:
      case OP_STRCAT:
      case OP_ARRAYSET:
      case OP_SUBSCRIPT:
      case OP_CLOSE_UPVALUE:
      case OP_MKGEN:
      case OP_YIELD:
      case OP_RESUME:
      case OP_SEND:
      case OP_AWAIT:
      case OP_SPAWN:
      case OP_RUN:
      case OP_SLEEP:
      case OP_DONE:
      case OP_RESULT:
      case OP_LEN:
      case OP_HASATTR:
      case OP_ASSERT:
      case OP_HLT:
        break;

      default:
        FAIL("Disassembling failed: unhandled opcode.");
    }

    printf("\n");
  }

  clock_gettime(CLOCK_MONOTONIC, &end);

  result.time =
      (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  return result;

#undef STRING_AT
#undef FAIL
#undef REQUIRE_OPERAND_BYTES
#undef READ_UINT8
#undef READ_INT16
#undef READ_DOUBLE_RAW
}
