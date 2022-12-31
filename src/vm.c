#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "math.h"
#include "compiler.h"
#include "vm.h"
#include "object.h"
#include "util.h"

void init_vm(VM *vm) {
    memset(vm, 0, sizeof(VM));
}

void free_vm(VM* vm) {
    /* Free the globals table and its strings. */
    table_free(&vm->globals);
    table_free(&vm->struct_blueprints); 
}

static inline void push(VM *vm, Object obj) {
    vm->stack[vm->tos++] = obj;
}

static inline Object pop(VM *vm) {
    return vm->stack[--vm->tos];
}

#define BINARY_OP(op, wrapper) \
do { \
    /* Operands are already on the stack. */ \
    Object b = pop(vm); \
    Object a = pop(vm); \
    Object obj = wrapper(TO_DOUBLE(a) op TO_DOUBLE(b)); \
    push(vm, obj); \
} while (0)

#define READ_UINT8() (*++(*ip))

#define READ_INT16() \
    /* ip points to one of the jump instructions and there \
     * is a 2-byte operand (offset) that comes after the jump \
     * instruction. We want to increment the ip so it points \
     * to the last of the two operands, and construct a 16-bit \
     * offset from the two bytes. Then ip is incremented in \
     * the loop again so it points to the next instruction \
     * (as opposed to pointing somewhere in the middle). */ \
    (*ip += 2, \
    (int16_t)(((*ip)[-1] << 8) | (*ip)[0]))

#define PRINT_STACK() \
do { \
    printf("stack: ["); \
    for (size_t i = 0; i < vm->tos; i++) { \
        PRINT_OBJECT(vm->stack[i]); \
        printf(", "); \
    } \
    printf("]\n"); \
} while(0)

#define RUNTIME_ERROR(...) \
do { \
    fprintf(stderr, "runtime error: "); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)

static inline void handle_op_print(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    Object object = pop(vm);
#ifdef venom_debug
    printf("dbg print :: ");
#endif
    PRINT_OBJECT(object);
    printf("\n");
    OBJECT_DECREF(object);
}

static inline void handle_op_const(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* At this point, ip points to OP_CONST.
    * Since this is a 2-byte instruction with an
    * immediate operand (the index of the double
    * constant in the constant pool), we want to
    * increment the ip to point to the index of
    * the constant in the constant pool that comes
    * after the opcode, and push the constant on
    * the stack. */
    uint8_t index = READ_UINT8();
    Object obj = AS_DOUBLE(chunk->cp[index]);
    push(vm, obj);
}

static inline void handle_op_get_global(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* At this point, ip points to OP_GET_GLOBAL.
    * Since this is a 2-byte instruction with an
    * immediate operand (the index of the name of
    * the looked up variable in the string constant
    * pool), we want to increment the ip so it points
    * to the /index/ of the string in the string
    * constant pool that comes after the opcode.
    * We then look up the variable and push its value
    * on the stack. If we can't find the variable,
    * we bail out. */
    uint8_t name_index = READ_UINT8();
    Object *obj = table_get(&vm->globals, chunk->sp[name_index]);
    if (obj == NULL) {
        RUNTIME_ERROR(
            "Variable '%s' is not defined",
            chunk->sp[name_index]
        );
    }
    push(vm, *obj);
    OBJECT_INCREF(*obj);
}

static inline void handle_op_set_global(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* At this point, ip points to OP_SET_GLOBAL.
    * This is a single-byte instruction that expects
    * two things to already be on the stack: the index
    * of the variable name in the string constant pool,
    * and the value of the double constant that the name
    * refers to. We pop these two and add the variable
    * to the globals table. */
    uint8_t name_index = READ_UINT8();
    Object constant = pop(vm);
    table_insert(&vm->globals, chunk->sp[name_index], constant);
}

static inline void handle_op_str(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* At this point, ip points to OP_CONST.
    * Since this is a 2-byte instruction with an
    * immediate operand (the index of the double
    * constant in the constant pool), we want to
    * increment the ip to point to the index of
    * the constant in the constant pool that comes
    * after the opcode, and push the constant on
    * the stack. */
    uint8_t index = READ_UINT8();
    Object obj = AS_STR(chunk->sp[index]);
    push(vm, obj);
}

static inline void handle_op_deep_set(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint8_t index = READ_UINT8();
    Object obj = pop(vm);
    int fp = vm->fp_stack[vm->fp_count-1];
    vm->stack[fp+index] = obj;
    OBJECT_DECREF(obj);
}

static inline void handle_op_deep_get(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint8_t index = READ_UINT8();
    int fp = vm->fp_stack[vm->fp_count-1];
    Object obj = vm->stack[fp+index];
    push(vm, obj);
    OBJECT_INCREF(obj);
}

static inline void handle_op_getattr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint8_t property_name_index = READ_UINT8();
    Object obj = pop(vm);
    Object *property = table_get(TO_STRUCT(obj).properties, chunk->sp[property_name_index]);
    if (property == NULL) {
        RUNTIME_ERROR(
            "Property '%s' is not defined on object '%s'",
            chunk->sp[property_name_index],
            TO_STRUCT(obj).name
        );
    }
    push(vm, *property);
    OBJECT_DECREF(obj);
    OBJECT_INCREF(*property);
}

static inline void handle_op_setattr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint8_t property_name_index = READ_UINT8();
    Object propety = pop(vm);
    Object value = pop(vm);
    table_insert(TO_STRUCT(propety).properties, chunk->sp[property_name_index], value);
}

static inline void handle_op_add(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(+, AS_DOUBLE);
}

static inline void handle_op_sub(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(-, AS_DOUBLE);
}

static inline void handle_op_mul(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(*, AS_DOUBLE);
}

static inline void handle_op_div(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(/, AS_DOUBLE);
}

static inline void handle_op_mod(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    Object b = pop(vm);
    Object a = pop(vm);
    Object obj = AS_DOUBLE(fmod(TO_DOUBLE(a), TO_DOUBLE(b)));
    push(vm, obj);
}

static inline void handle_op_gt(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(>, AS_BOOL);
}

static inline void handle_op_lt(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(<, AS_BOOL);
}

static inline void handle_op_eq(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(==, AS_BOOL);
}

static inline void handle_op_jz(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* Jump if zero. */
    int16_t offset = READ_INT16();
    Object obj = pop(vm);
    if (!TO_BOOL(obj)) {
        *ip += offset;
    }
}

static inline void handle_op_jmp(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    int16_t offset = READ_INT16();
    *ip += offset;
}

static inline void handle_op_negate(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    Object original = pop(vm);
    Object negated = AS_DOUBLE(-TO_DOUBLE(original));
    push(vm, negated);
}

static inline void handle_op_not(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    Object obj = pop(vm);
    push(vm, AS_BOOL(TO_BOOL(obj) ^ 1));
}

static inline void handle_op_func(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* At this point, ip points to OP_FUNC. 
     * After the opcode, there is the index
     * of the function's name in the string
     * constant pool, followed by the number
     * of function parameters. */
    uint8_t funcname_index = READ_UINT8();
    uint8_t paramcount = READ_UINT8();

    /* After the number of parameters, there
     * is one more byte: the location of the
     * function in the bytecode. */ 
    uint8_t location = READ_UINT8();

    /* We make the function object... */
    char *funcname = chunk->sp[funcname_index];
    Function func = {
        .location = location,
        .name = funcname,
        .paramcount = paramcount,
    };

    Object funcobj = AS_FUNC(func);

    /* ...and insert it into the 'vm->globals' table. */
    table_insert(&vm->globals, funcname, funcobj);
}

static inline void handle_op_invoke(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* We first read the index of the function name and the argcount. */
    uint8_t funcname = READ_UINT8();
    uint8_t argcount = READ_UINT8();

    /* Then, we look it up from the globals table. */ 
    Object *funcobj = table_get(&vm->globals, chunk->sp[funcname]);
    if (funcobj == NULL) {
        /* Runtime error if the function is not defined. */
        RUNTIME_ERROR(
            "Variable '%s' is not defined",
            chunk->sp[funcname]
        );
    }

    /* If the number of arguments the function was called with 
        + does not match the number of parameters the function was
        * declared to accept, raise a runtime error. */
    if (argcount != funcobj->as.func.paramcount) {
        RUNTIME_ERROR(
            "Function '%s' requires '%d' arguments.",
            chunk->sp[funcname], argcount
        );
    }

    /* Since we need the arguments after the instruction pointer,
        * pop them into a temporary array so we can push them back
        * after we place the instruction pointer on the stack. */
    Object arguments[256];
    for (int i = 0; i < argcount; i++) {
        arguments[i] = pop(vm);
    }
    
    /* Then, we push the return address on the stack. */
    Object ip_obj = AS_POINTER(*ip);
    push(vm, ip_obj);

    /* After that, we push the current frame pointer
     * on the frame pointer stack. */
    vm->fp_stack[vm->fp_count++] = vm->tos;

    /* Push the arguments back on the stack, but in reverse order. */
    for (int i = argcount-1; i >= 0; i--) {
        push(vm, arguments[i]);
    }
                    
    /* We modify ip so that it points to one instruction
     * just before the code we're invoking. */
    *ip = &chunk->code.data[TO_FUNC(*funcobj).location-1];
}

static inline void handle_op_ret(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* By the time we encounter OP_RET, the return
     * value is located on the stack. Beneath it are
     * the function arguments, followed by the return
     * address. */
    Object returnvalue = pop(vm);

    /* We pop the last frame pointer off the frame pointer stack. */
    int fp = vm->fp_stack[--vm->fp_count];

    /* Then, we clean up everything between the top of the stack
     * and the frame pointer we popped in the previous step. */
    int to_pop = vm->tos - fp;
    for (int i = 0; i < to_pop; i++) {
        Object obj = pop(vm);
        OBJECT_DECREF(obj);
    }

    /* After the arguments comes the return address which we'll
     * use to modify the instruction pointer ip and return to the
     * caller. */
    Object returnaddr = pop(vm);

    /* Then, we push the return value back on the stack.  */
    push(vm, returnvalue);
    OBJECT_DECREF(returnvalue);

    /* Finally, we modify the instruction pointer. */
    *ip = returnaddr.as.ptr;
}

static inline void handle_op_true(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    push(vm, AS_BOOL(true));
}

static inline void handle_op_null(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    push(vm, AS_NULL());
}

static inline void handle_op_struct(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint8_t struct_name = READ_UINT8();
    uint8_t property_count = READ_UINT8();

    StructBlueprint sb = { 
        .name = chunk->sp[struct_name],
        .propertycount = property_count,
    };

    for (int i = 0; i < property_count; i++) {
        uint8_t property_name = READ_UINT8();
        dynarray_insert(&sb.properties, chunk->sp[property_name]);
    }

    table_insert(
        &vm->struct_blueprints,
        chunk->sp[struct_name],
        AS_STRUCT_BLUEPRINT(sb)
    );
}

static inline void handle_op_struct_init(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint8_t structname = READ_UINT8();

    Object *blueprint = table_get(&vm->struct_blueprints, chunk->sp[structname]);
    if (blueprint == NULL) {
        RUNTIME_ERROR(
            "Struct '%s' is not defined",
            chunk->sp[structname]
        );
    }

    uint8_t propertycount = READ_UINT8();
    if (propertycount != blueprint->as.struct_blueprint.propertycount) {
        RUNTIME_ERROR(
            "Incorrect property count for struct '%s'",
            TO_STRUCT_BLUEPRINT(*blueprint).name
        );
    }

    Struct s = {
        .name = TO_STRUCT_BLUEPRINT(*blueprint).name,
        .propertycount = TO_STRUCT_BLUEPRINT(*blueprint).propertycount,
        .properties = malloc(sizeof(Table)),
    };

    memset(s.properties, 0, sizeof(Table));

    Object structobj = AS_STRUCT(s);

    HeapObject heapobj = {
        .refcount = 1,
        .obj = structobj,
    };

    push(vm, AS_HEAP(ALLOC(heapobj)));
}

static inline void handle_op_struct_init_finalize(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint8_t propertycount = READ_UINT8();

    Object property_names[256];
    Object property_values[256];                
    for (size_t i = 0; i < propertycount; i++) {
        property_values[i] = pop(vm);
        property_names[i] = pop(vm);
    }

    Object structobj = pop(vm);

    for (size_t i = 0; i < propertycount; i++) {
        table_insert(TO_STRUCT(structobj).properties, TO_PROP(property_names[i]), property_values[i]);
    }

    push(vm, structobj);
}

static inline void handle_op_prop(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint8_t propertyname_index = READ_UINT8();
    push(vm, AS_PROP(chunk->sp[propertyname_index]));
}

typedef void (*Handler)(VM *vm, BytecodeChunk *chunk, uint8_t **ip);

Handler dispatcher[] = {
    [OP_PRINT] = handle_op_print,
    [OP_CONST] = handle_op_const,
    [OP_GET_GLOBAL] = handle_op_get_global,
    [OP_SET_GLOBAL] = handle_op_set_global,
    [OP_STR] = handle_op_str,
    [OP_DEEP_GET] = handle_op_deep_get,
    [OP_DEEP_SET] = handle_op_deep_set,
    [OP_GETATTR] = handle_op_getattr,
    [OP_SETATTR] = handle_op_setattr,
    [OP_ADD] = handle_op_add,
    [OP_SUB] = handle_op_sub,
    [OP_MUL] = handle_op_mul,
    [OP_DIV] = handle_op_div,
    [OP_MOD] = handle_op_mod,
    [OP_GT] = handle_op_gt,
    [OP_LT] = handle_op_lt,
    [OP_EQ] = handle_op_eq,
    [OP_JZ] = handle_op_jz,
    [OP_JMP] = handle_op_jmp,
    [OP_NEGATE] = handle_op_negate,
    [OP_NOT] = handle_op_not,
    [OP_FUNC] = handle_op_func,
    [OP_INVOKE] = handle_op_invoke,
    [OP_RET] = handle_op_ret,
    [OP_TRUE] = handle_op_true,
    [OP_NULL] = handle_op_null,
    [OP_STRUCT] = handle_op_struct,
    [OP_STRUCT_INIT] = handle_op_struct_init,
    [OP_STRUCT_INIT_FINALIZE] = handle_op_struct_init_finalize,
    [OP_PROP] = handle_op_prop,
};

void print_current_instruction(uint8_t *ip) {
    #ifdef venom_debug
        printf("current instruction: ");
        switch (*ip) {
            case OP_PRINT: printf("OP_PRINT"); break;
            case OP_ADD: printf("OP_ADD"); break;
            case OP_SUB: printf("OP_SUB"); break;
            case OP_MUL: printf("OP_MUL"); break;
            case OP_DIV: printf("OP_DIV"); break;
            case OP_MOD: printf("OP_MOD"); break;
            case OP_EQ: printf("OP_EQ"); break;
            case OP_GT: printf("OP_GT"); break;
            case OP_LT: printf("OP_LT"); break;
            case OP_NOT: printf("OP_NOT"); break;
            case OP_NEGATE: printf("OP_NEGATE"); break;
            case OP_JMP: printf("OP_JMP"); break;
            case OP_JZ: printf("OP_JZ"); break;
            case OP_FUNC: printf("OP_FUNC"); break;
            case OP_INVOKE: printf("OP_INVOKE"); break;
            case OP_RET: printf("OP_RET"); break;
            case OP_CONST: printf("OP_CONST"); break;
            case OP_STR: printf("OP_STR"); break;
            case OP_SET_GLOBAL: printf("OP_SET_GLOBAL"); break;
            case OP_GET_GLOBAL: printf("OP_GET_GLOBAL"); break;
            case OP_STRUCT: printf("OP_STRUCT"); break;
            case OP_STRUCT_INIT: printf("OP_STRUCT_INIT"); break;
            case OP_STRUCT_INIT_FINALIZE: printf("OP_STRUCT_INIT_FINALIZE"); break;
            case OP_DEEP_SET: printf("OP_DEEP_SET: %d", ip[1]); break; 
            case OP_DEEP_GET: printf("OP_DEEP_GET: %d", ip[1]); break;
            case OP_GETATTR: printf("OP_GETATTR"); break;
            case OP_SETATTR: printf("OP_SETATTR"); break;
            case OP_PROP: printf("OP_PROP"); break;
            case OP_NULL: printf("OP_NULL"); break;
        }
        printf("\n");
#endif

}

int run(VM *vm, BytecodeChunk *chunk) {
#ifdef venom_debug
    disassemble(chunk);
#endif

    for (
        uint8_t *ip = chunk->code.data;
        ip < &chunk->code.data[chunk->code.count];  /* ip < addr of just beyond the last instruction */
        ip++
    ) {
#ifdef venom_debug
        print_current_instruction(ip);
#endif
        dispatcher[*ip](vm, chunk, &ip);

#ifdef venom_debug
        PRINT_STACK();
#endif
    }

    return 0;
}