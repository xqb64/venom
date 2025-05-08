#ifndef venom_compiler_h
#define venom_compiler_h

#include <stddef.h>
#include <stdint.h>

#include "dynarray.h"
#include "object.h"
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
  OP_NEG,
  OP_TRUE,
  OP_NULL,
  OP_CONST,
  OP_STR,
  OP_JMP,
  OP_JZ,
  OP_BITAND,
  OP_BITOR,
  OP_BITXOR,
  OP_BITNOT,
  OP_BITSHL,
  OP_BITSHR,
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
  OP_CLOSURE,
  OP_CALL,
  OP_CALL_METHOD,
  OP_RET,
  OP_POP,
  OP_DEREF,
  OP_DEREFSET,
  OP_STRCAT,
  OP_ARRAY,
  OP_ARRAYSET,
  OP_SUBSCRIPT,
  OP_GET_UPVALUE,
  OP_GET_UPVALUE_PTR,
  OP_SET_UPVALUE,
  OP_CLOSE_UPVALUE,
  OP_IMPL,
  OP_MKGEN,
  OP_YIELD,
  OP_RESUME,
  OP_LEN,
  OP_HASATTR,
  OP_ASSERT,
  OP_HLT,
} Opcode;

typedef struct Bytecode {
  DynArray_uint8_t code;
  DynArray_char_ptr sp; /* string pool */
} Bytecode;

typedef Table(int) Table_int;
typedef Table(Function *) Table_FunctionPtr;

typedef struct {
  char *name;
  Table_int *property_indexes;
  Table_FunctionPtr *methods;
} StructBlueprint;

typedef Table(StructBlueprint) Table_StructBlueprint;
void free_table_struct_blueprints(Table_StructBlueprint *table);

typedef struct {
  char *name;
  int depth;
  bool captured;
} Local;

typedef struct {
  int location;
  int patch_with;
} Label;

typedef Table(Label) Table_Label;

typedef struct Compiler {
  Local locals[256];
  size_t locals_count;
  Local globals[256];
  size_t globals_count;
  Table_Function *functions;
  Table_Label *labels;
  DynArray_int loop_depths;
  Table_StructBlueprint *struct_blueprints;
  int depth;
  Function *current_fn;
  struct Compiler *next;
  DynArray_int upvalues;
  Table_FunctionPtr *builtins;
} Compiler;

typedef struct {
  Bytecode *chunk;
  bool is_ok;
  char *msg;
} CompileResult;

void init_chunk(Bytecode *code);
void free_chunk(Bytecode *code);

Compiler *new_compiler(void);
void free_compiler(Compiler *compiler);

CompileResult compile(const DynArray_Stmt *ast);

void free_compile_result(CompileResult *result);

extern Compiler *current_compiler;

#endif
