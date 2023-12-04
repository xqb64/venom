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
  OP_BITWISE_AND,
  OP_BITWISE_OR,
  OP_BITWISE_XOR,
  OP_BITWISE_NOT,
  OP_BITWISE_SHIFT_LEFT,
  OP_BITWISE_SHIFT_RIGHT,
  OP_TRUE,
  OP_NULL,
  OP_CONST,
  OP_STR,
  OP_JMP,
  OP_JZ,
  OP_SET_GLOBAL,
  OP_GET_GLOBAL,
  OP_GET_GLOBAL_PTR,
  OP_DEEPSET,
  OP_DEEPGET,
  OP_DEEPGET_PTR,
  OP_SETATTR,
  OP_GETATTR,
  OP_GETATTR_PTR,
  OP_STRUCT,
  OP_STRUCT_BLUEPRINT,
  OP_CALL,
  OP_RET,
  OP_POP,
  OP_DUP,
  OP_DEREF,
  OP_DEREFSET,
  OP_STRCAT,
} Opcode;

typedef DynArray(uint8_t) DynArray_uint8_t;
typedef DynArray(double) DynArray_double;

typedef struct BytecodeChunk {
  DynArray_uint8_t code;
  DynArray_double cp;   /* constant pool */
  DynArray_char_ptr sp; /* string pool */
} BytecodeChunk;

typedef struct {
  char *name;
  size_t location;
  size_t paramcount;
} Function;

typedef Table(int) Table_int;

typedef struct {
  char *name;
  Table_int *property_indexes;
} StructBlueprint;

typedef Table(Function) Table_Function;
typedef Table(StructBlueprint) Table_StructBlueprint;
void free_table_struct_blueprints(Table_StructBlueprint *table);
void free_table_functions(Table_Function *table);

typedef struct Compiler {
  Table_Function *functions;
  Table_StructBlueprint *struct_blueprints;
  DynArray_char_ptr globals;
  DynArray_char_ptr locals;
  DynArray_int breaks;
  DynArray_int loop_starts;
  int depth;
  int pops[POPS_MAX];
} Compiler;

void init_chunk(BytecodeChunk *chunk);
void free_chunk(BytecodeChunk *chunk);
void init_compiler(Compiler *compiler);
void free_compiler(Compiler *compiler);
void compile(Compiler *compiler, BytecodeChunk *chunk, Statement stmt);

#endif