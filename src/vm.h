#ifndef venom_vm_h
#define venom_vm_h

#define STACK_MAX 1024

#include <stddef.h>

#include "compiler.h"
#include "object.h"

typedef Table(Function *) Table_FunctionPtr;

typedef struct
{
    Object stack[STACK_MAX];
    size_t tos; /* top of stack */
    Table_FunctionPtr functions;
    Table_Object globals;
    Table_StructBlueprint *blueprints;
    BytecodePtr fp_stack[STACK_MAX]; /* a stack for frame pointers */
    size_t fp_count;
    Upvalue *upvalues;
} VM;

void init_vm(VM *vm);
void free_vm(VM *vm);
void run(VM *vm, Bytecode *code);

#endif
