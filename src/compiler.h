#ifndef venom_compiler_h
#define venom_compiler_h

#define POPS_MAX 256

#include <stddef.h>
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
  OP_BITAND,
  OP_BITOR,
  OP_BITXOR,
  OP_BITNOT,
  OP_BITSHL,
  OP_BITSHR,
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
  OP_IMPL,
  OP_CALL,
  OP_CALL_METHOD,
  OP_RET,
  OP_POP,
  OP_DEREF,
  OP_DEREFSET,
  OP_STRCAT,
  OP_ARRAY,
  OP_SUBSCRIPT,
} Opcode;

typedef DynArray(uint8_t) DynArray_uint8_t;
typedef DynArray(double) DynArray_double;

typedef struct Bytecode {
  DynArray_uint8_t code;
  DynArray_double cp;   /* constant pool */
  DynArray_char_ptr sp; /* string pool */
} Bytecode;

typedef struct {
  char *name;
  size_t location;
  size_t paramcount;
} Function;

typedef Table(int) Table_int;
typedef Table(Function) Table_Function;

typedef struct {
  char *name;
  Table_int *property_indexes;
  Table_Function *methods;
} StructBlueprint;

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
  DynArray_int loop_depths;
  int depth;
  int pops[POPS_MAX];
} Compiler;

void init_chunk(Bytecode *code);
void free_chunk(Bytecode *code);
void init_compiler(Compiler *compiler);
void free_compiler(Compiler *compiler);
void compile(Compiler *compiler, Bytecode *code, Stmt stmt);

#endif
