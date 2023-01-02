#ifndef venom_compiler_h
#define venom_compiler_h

#define POOL_MAX 256

#include <stdint.h>
#include "dynarray.h"
#include "parser.h"

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
    OP_NEGATE,
    OP_JMP,
    OP_JZ,
    OP_FUNC,
    OP_STRUCT,
    OP_STRUCT_INIT,
    OP_STRUCT_INIT_FINALIZE,
    OP_PROP,
    OP_INVOKE,
    OP_RET,
    OP_CONST,
    OP_STR,
    OP_TRUE,
    OP_NULL,
    OP_SET_GLOBAL,
    OP_GET_GLOBAL,
    OP_DEEP_SET,
    OP_DEEP_GET,
    OP_GETATTR,
    OP_SETATTR,
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
    char *locals[256];
    int locals_count;
    int jmp_stack[256];
    size_t jmp_tos;
    int backjmp_stack[256];
    size_t backjmp_tos;
} Compiler;

void init_chunk(BytecodeChunk *chunk);
void free_chunk(BytecodeChunk *chunk);
void compile(Compiler *compiler, BytecodeChunk *chunk, Statement stmt, bool scoped);
void disassemble(BytecodeChunk *chunk);
void init_compiler(Compiler *compiler);

#endif