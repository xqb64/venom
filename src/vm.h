#ifndef venom_vm_h
#define venom_vm_h

#define STACK_MAX 255

#include "compiler.h"

typedef struct {
    double stack[STACK_MAX];
    size_t tos;  /* top of stack */
    double *cp;  /* constant pool */
    size_t cpp;  /* constant pool pointer */
} VM;

typedef struct BytecodeChunk BytecodeChunk;

void init_vm(VM *vm);
void free_vm(VM *vm);
void run(VM *vm, BytecodeChunk *chunk);

#endif