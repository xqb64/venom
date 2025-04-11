#ifndef venom_compiler_h
#define venom_compiler_h

#define POPS_MAX 256

#include <stddef.h>
#include <stdint.h>

#include "dynarray.h"
#include "object.h"
#include "parser.h"

typedef enum
{
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

typedef DynArray(uint8_t) DynArray_uint8_t;
typedef DynArray(double) DynArray_double;

typedef struct Bytecode
{
    DynArray_uint8_t code;
    DynArray_char_ptr sp; /* string pool */
} Bytecode;

typedef Table(int) Table_int;
typedef Table(Function *) Table_FunctionPtr;

typedef struct
{
    char *name;
    Table_int *property_indexes;
    Table_FunctionPtr *methods;
} StructBlueprint;

typedef Table(StructBlueprint) Table_StructBlueprint;
void free_table_struct_blueprints(Table_StructBlueprint *table);
void free_table_functions(Table_Function *table);

typedef DynArray(struct module *) DynArray_module_ptr;

struct module
{
    char *path;
    DynArray_module_ptr imports;
    struct module *parent;
    Bytecode code;
};

typedef Table(struct module *) Table_module_ptr;

typedef struct
{
    char *name;
    int depth;
    bool captured;
} Local;

typedef struct Compiler
{
    Local locals[256];
    size_t locals_count;
    Local globals[256];
    size_t globals_count;
    Table_Function *functions;
    DynArray_int breaks;
    DynArray_int loop_starts;
    DynArray_int loop_depths;
    Table_StructBlueprint *struct_blueprints;
    Table_module_ptr *compiled_modules;
    int depth;
    struct module *current_mod;
    char *root_mod;
    Function *current_fn;
    struct Compiler *next;
    DynArray_int upvalues;
    Table_FunctionPtr *builtins;
} Compiler;

void init_chunk(Bytecode *code);
void free_chunk(Bytecode *code);

Compiler *new_compiler(void);
void free_compiler(Compiler *compiler);

void compile(Bytecode *code, Stmt stmt);

void emit_stack_cleanup(Bytecode *code);

extern Compiler *current_compiler;

#endif