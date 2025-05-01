#ifndef venom_vm_h
#define venom_vm_h

#define STACK_MAX 1024

#include <stddef.h>

#include "compiler.h"
#include "object.h"

typedef struct
{
    Object stack[STACK_MAX];
    size_t tos;
    BytecodePtr fp_stack[STACK_MAX];
    size_t fp_count;
    uint8_t *ip;
} FrameSnapshot;

typedef struct
{
    Object stack[STACK_MAX];
    size_t tos; /* top of stack */
    Table_Object globals;
    Table_StructBlueprint *blueprints;
    BytecodePtr fp_stack[STACK_MAX]; /* a stack for frame pointers */
    size_t fp_count;
    size_t gen_count;
    Upvalue *upvalues;
    Generator *gen_stack[STACK_MAX];
    FrameSnapshot *fs_stack[STACK_MAX];
    size_t fs_count;
} VM;

typedef struct
{
    bool is_ok;
    char *msg;
} ExecResult;

void init_vm(VM *vm);
void free_vm(VM *vm);
ExecResult exec(VM *vm, Bytecode *code);

#endif
