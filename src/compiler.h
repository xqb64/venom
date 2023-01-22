#ifndef venom_compiler_h
#define venom_compiler_h

#define POPS_MAX 256

#include <stdint.h>
#include "dynarray.h"
#include "parser.h"
#include "table.h"

typedef enum {
    OP_PRINT,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_EQ,
    OP_GT,
    OP_LT,
    OP_NOT,
    OP_NEG,
    OP_JMP,
    OP_JZ,
    OP_STRUCT,
    OP_RET,
    OP_CONST,
    OP_STR,
    OP_TRUE,
    OP_NULL,
    OP_SET_GLOBAL,
    OP_SET_GLOBAL_DEREF,
    OP_GET_GLOBAL,
    OP_DEEPSET,
    OP_DEEPSET_DEREF,
    OP_DEEPGET,
    OP_GETATTR,
    OP_SETATTR,
    OP_IP,
    OP_INC_FPCOUNT,
    OP_POP,
    OP_ADDR,
    OP_ADDR_GLOBAL,
    OP_DEREF,
} Opcode;

typedef DynArray(uint8_t) DynArray_uint8_t;
typedef DynArray(double) DynArray_double;

typedef struct BytecodeChunk {
    DynArray_uint8_t code;
    DynArray_double cp;     /* constant pool */
    DynArray_char_ptr sp;   /* string pool */
} BytecodeChunk;

typedef struct Compiler {
    Table functions;
    Table structs;
    DynArray_char_ptr globals;
    DynArray_char_ptr locals;
    DynArray_int breaks;
    DynArray_int continues;
    int depth;
    int pops[POPS_MAX];
} Compiler;

void init_chunk(BytecodeChunk *chunk);
void free_chunk(BytecodeChunk *chunk);
void init_compiler(Compiler *compiler);
void free_compiler(Compiler *compiler);
void compile(Compiler *compiler, BytecodeChunk *chunk, Statement stmt);

#endif