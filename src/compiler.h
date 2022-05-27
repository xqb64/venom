#ifndef venom_compiler_h
#define venom_compiler_h

#define POOL_MAX 256

#include <stdint.h>
#include "dynarray.h"
#include "parser.h"
#include "object.h"
#include "table.h"

typedef enum {
    OP_PRINT,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_EQ,
    OP_GT,
    OP_LT,
    OP_NOT,
    OP_NEGATE,
    OP_JMP,
    OP_JZ,
    OP_FUNC,
    OP_INVOKE,
    OP_RET,
    OP_CONST,
    OP_STR_CONST,
    OP_SET_GLOBAL,
    OP_GET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_POP,
    OP_DEEP_SET,
    OP_EXIT,
} Opcode;

typedef DynArray(uint8_t) Uint8DynArray;

typedef struct BytecodeChunk {
    Uint8DynArray code;
    double cp[POOL_MAX];  /* constant pool */
    char *sp[POOL_MAX];   /* string pool */
    uint8_t cp_count;
    uint8_t sp_count;
} BytecodeChunk;

typedef struct {
    Uint8DynArray stack_sizes;
    int paramcount;
    String_DynArray locals;
    Table functions;
    int stack[256];
    int tos;
} Compiler;

void init_chunk(BytecodeChunk *chunk);
void free_chunk(BytecodeChunk *chunk);
void first_pass(Compiler *compiler, BytecodeChunk *chunk, Statement stmt, bool scoped);
void compile(Compiler *compiler, BytecodeChunk *chunk, Statement stmt, bool scoped);
void disassemble(BytecodeChunk *chunk);
void init_compiler(Compiler *compiler);
void backpatch_stack_size(Compiler *compiler, BytecodeChunk *chunk, int index, int size, char *prefix);

#endif