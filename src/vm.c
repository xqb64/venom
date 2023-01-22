#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "math.h"
#include "compiler.h"
#include "object.h"
#include "util.h"
#include "vm.h"

void init_vm(VM *vm) {
    memset(vm, 0, sizeof(VM));
}

void free_vm(VM* vm) {
    /* Free the globals table and its strings. */
    table_free(&vm->globals);
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

#define READ_UINT32() \
    (*ip += 4, \
    (uint32_t)(((*ip)[-3] << 24) | ((*ip)[-2] << 16) | ((*ip)[-1] << 8) | (*ip)[0]))

#define PRINT_STACK() \
do { \
    printf("stack: ["); \
    for (size_t i = 0; i < vm->tos; i++) { \
        PRINT_OBJECT(vm->stack[i]); \
        if (i < vm->tos-1) { \
            printf(", "); \
        } \
    } \
    printf("]\n"); \
} while(0)

#define RUNTIME_ERROR(...) \
do { \
    fprintf(stderr, "runtime error: "); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
    return 1; \
} while (0)

static inline bool check_equality(Object *left, Object *right) {
    /* Return false if the objects are of different type. */
    if (left->type != right->type) {
        return false;
    }

    /* Return true if both objects are nulls. */
    if (IS_NULL(*left) && IS_NULL(*right)) {
        return true;
    }

    /* If both objects are booleans, compare them. */
    if (IS_BOOL(*left) && IS_BOOL(*right)) {
        return TO_BOOL(*left) == TO_BOOL(*right);
    }

    /* If both objects are numbers, compare them. */
    if (IS_NUM(*left) && IS_NUM(*right)) {
        return TO_DOUBLE(*left) == TO_DOUBLE(*right);
    }

    /* If both objects are strings, compare them. */
    if (IS_STRING(*left) && IS_STRING(*right)) {
        return strcmp(TO_STR(*left), TO_STR(*right)) == 0;
    }

    /* If both objects are structs, compare them. */
    if (IS_STRUCT(*left) && IS_STRUCT(*right)) {
        Struct *a = TO_STRUCT(*left);
        Struct *b = TO_STRUCT(*right);

        /* Return false if the structs are of different types. */
        if (strcmp(a->name, b->name) != 0) {
            return false;
        }

        /* If they are the same type, iterate over the
         * 'properties' Table in struct 'a', and for each
         * property that is not NULL, run the func recursively
         * comparing that property with the corresponding
         * property in struct 'b'. */
        for (size_t i = 0; i < TABLE_MAX; i++) {
            if (a->properties->data[i] == NULL) continue;
            char *key = a->properties->data[i]->key;
            if (!check_equality(
                table_get(a->properties, key),
                table_get(b->properties, key)
            )) {
                return false;
            }
        }
        /* We haven't returned false while iterating over
         * the table, which means the properties are equal,
         * and so are the two structs. */
        return true;
    }
    assert(0);
}

static inline int handle_op_print(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_PRINT pops an object off the stack and prints it.
     * Since the popped object might be refcounted, the re-
     * ference count must be decremented. */
    Object object = pop(vm);
#ifdef venom_debug_vm
    printf("dbg print :: ");
#endif
    PRINT_OBJECT(object);
    printf("\n");
    OBJECT_DECREF(object);
    return 0;
}

static inline int handle_op_const(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_CONST reads a 4-byte index of the constant in the
     * chunk's cp, constructs an object with that value and
     * pushes it on the stack. Since constants are not ref-
     * counted, incrementing the refcount is not needed. */
    uint32_t idx = READ_UINT32();
    Object obj = AS_DOUBLE(chunk->cp.data[idx]);
    push(vm, obj);
    return 0;
}

static inline int handle_op_get_global(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_GET_GLOBAL reads a 4-byte index of the variable name
     * in the chunk's sp, looks up the object with that name in
     * in the vm's globals tablem and pushes it on the stack.
     * Since the object will be present in yet another location,
     * the refcount must be incremented. */
    uint32_t name_idx = READ_UINT32();
    Object *obj = table_get(&vm->globals, chunk->sp.data[name_idx]);
    push(vm, *obj);
    OBJECT_INCREF(*obj);
    return 0;
}

static inline int handle_op_set_global(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_SET_GLOBAL reads a 4-byte index of the variable name
     * in the chunk's sp, pops an object off the stack and in-
     * serts it into the vm's globals table under that name. */
    uint32_t name_idx = READ_UINT32();
    Object obj = pop(vm);
    table_insert(&vm->globals, chunk->sp.data[name_idx], obj);
    return 0;
}

static inline int handle_op_set_global_deref(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_SET_GLOBAL reads a 4-byte index of the variable name
     * in the chunk's sp, pops an object off the stack and in-
     * serts it into the vm's globals table under that name. */
    uint8_t deref_count = READ_UINT8();
    uint32_t name_idx = READ_UINT32();
    Object obj = pop(vm);
    Object *target = table_get(&vm->globals, chunk->sp.data[name_idx]);
    for (int i = 0; i < deref_count; i++) {
        target = target->as.ptr;
    }
    *target = obj;
    return 0;
}


static inline int handle_op_str(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_STR reads a 4-byte index of the string in the chunk's
     * sp, constructs an object with that value and pushes it on
     * the stack. */
    uint32_t idx = READ_UINT32();
    Object obj = AS_STR(chunk->sp.data[idx]);
    push(vm, obj);
    return 0;
}

static inline int handle_op_deepset(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_DEEPSET reads a 4-byte index (1-based) of the object
     * being modified, pops an object off the stack, fetches
     * the current frame pointer from the frame pointer stack,
     * and setting index'th item after the frame pointer to the
     * popped object.
     *
     * However, since indexes are 1-based, the index needs to be
     * adjusted by subtracting 1 if there are no frame pointers
     * on the stack.
     * 
     * Since the object being set will be overwritten, its refcount
     * must be decremented before placing the popped object into
     * that position. */
    uint32_t idx = READ_UINT32();
    Object obj = pop(vm);
    int fp = vm->fp_stack[vm->fp_count-1];
    int adjustment = vm->fp_count == 0 ? -1 : 0;
    OBJECT_DECREF(vm->stack[fp+idx+adjustment]);
    vm->stack[fp+idx+adjustment] = obj;
    return 0;
}

static inline int handle_op_deepset_deref(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_DEEPSET_DEREF reads a 1-byte dereference count and
     * a 4-byte index (1-based) of the object being modified
     * (which must be a pointer). To access the pointer, the
     * 4-byte index is adjusted to be relative to the curre-
     * nt frame pointer ('adjustment' takes care of the case
     * where there are no frame pointers on the stack). Then
     * the pointer is followed (and dereferenced on the way)
     * 'deref_count' times and its value set to the previou-
     * sly popped object.
     *
     * Considering that the object being set will be overwr-
     * itten, its reference count must be decremented before
     * putting the popped object into that position. */
    uint8_t deref_count = READ_UINT8();
    uint32_t idx = READ_UINT32();
    Object obj = pop(vm);
    int fp = vm->fp_stack[vm->fp_count-1];
    int adjustment = vm->fp_count == 0 ? -1 : 0;
    Object *target = &vm->stack[fp+idx+adjustment];
    for (int i = 0; i < deref_count; i++) {
        target = target->as.ptr;
    }
    OBJECT_DECREF(*target);
    *target = obj;
    return 0;
}

static inline int handle_op_deepget(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_DEEPGET reads a 4-byte index (1-based) of the obj-
     * ect being accessed, which is then adjusted to be rel-
     * ative to the current frame pointer ('adjustment' tak-
     * es care of the case where there are no frame pointers
     * on the stack). Then it uses the adjusted index to get
     * the object in that position and push it on the stack.
     *
     * Since the object being accessed will now be available
     * in yet another location, its refcount must be increm-
     * ented. */
    uint32_t idx = READ_UINT32();
    int fp = vm->fp_stack[vm->fp_count-1];
    int adjustment = vm->fp_count == 0 ? -1 : 0;
    Object obj = vm->stack[fp+idx+adjustment];
    push(vm, obj);
    OBJECT_INCREF(obj);
    return 0;
}

static inline int handle_op_getattr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_GETATTR reads a 1-byte number (effectively a boolean
     * value) that determines whether to push a pointer to the
     * property (or the property itself) on the stack. Besides
     * this number, it also reads a 4-byte index of the prope-
     * rty name in the chunk's sp. Then, it pops an object off
     * the stack and uses it to look up the property under th-
     * at name in the object's properties Table. If the prope-
     * rty is found, depending on the previously read bool va-
     * lue, it (or the pointer that points to it) is pushed on
     * the stack. Otherwise, a runtime error is raised. */
    bool ptr = !!READ_UINT8();
    uint32_t property_name_idx = READ_UINT32();
    Object obj = pop(vm);
    Object *property = table_get(TO_STRUCT(obj)->properties, chunk->sp.data[property_name_idx]);
    if (property == NULL) {
        RUNTIME_ERROR(
            "Property '%s' is not defined on object '%s'",
            chunk->sp.data[property_name_idx],
            TO_STRUCT(obj)->name
        );
    }
    if (ptr) {
        push(vm, AS_PTR(property));
    } else {
        push(vm, *property);
        OBJECT_INCREF(*property);
    }
    OBJECT_DECREF(obj);
    return 0;
}

static inline int handle_op_setattr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_SETATTR reads a 4-byte index of the property name in
     * the chunk's sp, pops two objects off the stack (a value
     * of the property, and the object being modified itself),
     * and inserts the value into the object's properties Table.
     * Then it pushes the modified object back on the stack. */
    uint32_t property_name_idx = READ_UINT32();
    Object value = pop(vm);
    Object structobj = pop(vm);
    table_insert(TO_STRUCT(structobj)->properties, chunk->sp.data[property_name_idx], value);
    push(vm, structobj);
    return 0;
}

static inline int handle_op_add(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(+, AS_DOUBLE);
    return 0;
}

static inline int handle_op_sub(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(-, AS_DOUBLE);
    return 0;
}

static inline int handle_op_mul(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(*, AS_DOUBLE);
    return 0;
}

static inline int handle_op_div(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(/, AS_DOUBLE);
    return 0;
}

static inline int handle_op_mod(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    Object b = pop(vm);
    Object a = pop(vm);
    Object obj = AS_DOUBLE(fmod(TO_DOUBLE(a), TO_DOUBLE(b)));
    push(vm, obj);
    return 0;
}

static inline int handle_op_gt(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(>, AS_BOOL);
    return 0;
}

static inline int handle_op_lt(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(<, AS_BOOL);
    return 0;
}

static inline int handle_op_eq(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    Object b = pop(vm);
    Object a = pop(vm);
    /* Since the two objects might be refcounted,
     * the reference count must be decremented. */
    OBJECT_DECREF(a);
    OBJECT_DECREF(b);
    push(vm, AS_BOOL(check_equality(&a, &b)));
    return 0;
}

static inline int handle_op_jz(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_JZ reads a signed 2-byte offset, pops an object off
     * the stack, and increments the instruction pointer by the
     * offset if the popped value was falsey.
     *
     * NOTE: the offset could be negative. */
    int16_t offset = READ_INT16();
    Object obj = pop(vm);
    if (!TO_BOOL(obj)) {
        *ip += offset;
    }
    return 0;
}

static inline int handle_op_jmp(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_JMP reads a signed 2-byte offset and increments
     * the instruction pointer by that amount.
     *
     * Unlike OP_JZ which is a conditional jump, OP_JMP
     * jumps unconditionally.
     *
     * NOTE: the offset could be negative. */
    int16_t offset = READ_INT16();
    *ip += offset;
    return 0;
}

static inline int handle_op_neg(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_NEGATE pops an object off the stack, negates
     * it and pushes the negative back on the stack. */
    Object original = pop(vm);
    Object negated = AS_DOUBLE(-TO_DOUBLE(original));
    push(vm, negated);
    return 0;
}

static inline int handle_op_not(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_NOT pops an object off the stack and pushes
     * its inverse back on the stack. */
    Object obj = pop(vm);
    push(vm, AS_BOOL(TO_BOOL(obj) ^ 1));
    return 0;
}

static inline int handle_op_ip(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_IP reads a signed 2-byte offset from the bytecode,
     * builds a pointer object that points to the instruction
     * that comes after the function call dance, updates the
     * frame pointer stack and pushes the pointer on the main
     * vm stack.
     *
     * NOTE: vm->fp_stack should be updated first before pushing
     * the pointer on the stack, because OP_DEEPGET/OP_DEEPSET
     * expect frame pointer indexing to be zero-based. */
    int16_t offset = READ_INT16();
    Object ip_obj = AS_BCPTR(*(ip)+offset);
    vm->fp_stack[vm->fp_count] = vm->tos;
    push(vm, ip_obj);
    return 0;
}

static inline int handle_op_inc_fpcount(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_INC_FPCOUNT increments vm->fp_count, effectively
     * putting an end to the function call dance. */
    vm->fp_count++;
    return 0;
}

static inline int handle_op_ret(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_RET pops the return value and the return address off
     * the stack, decrements vm->fp_count, pushes the return
     * value back on the stack, and modifies the instruction
     * pointer to point to the return address. */
    Object returnvalue = pop(vm);
    Object returnaddr = pop(vm);
    --vm->fp_count;
    push(vm, returnvalue);
    *ip = returnaddr.as.bcptr;
    return 0;
}

static inline int handle_op_true(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_TRUE pushes a boolean object ('true') on the stack. */
    push(vm, AS_BOOL(true));
    return 0;
}

static inline int handle_op_null(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_NULL pushes a null object on the stack. */
    push(vm, AS_NULL());
    return 0;
}

static inline int handle_op_pop(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_POP pops an object off the stack. Since the popped
     * object might be refcounted, the reference count must
     * be decremented. */
    Object obj = pop(vm);
    OBJECT_DECREF(obj);
    return 0;
}

static inline int handle_op_struct(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_STRUCT reads a 4-byte index of the struct name in the
     * chunk's sp, builds a struct object with that name and with
     * refcount set to 1 (while making sure to initialize the pr-
     * operties table properly), and pushes it on the stack. */
    uint32_t structname = READ_UINT32();

    Struct s = {
        .name = chunk->sp.data[structname],
        .properties = malloc(sizeof(Table)),
        .refcount = 1,
    };

    memset(s.properties, 0, sizeof(Table));

    push(vm, AS_STRUCT(ALLOC(s)));
    return 0;
}

static inline int handle_op_addr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint32_t idx = READ_UINT32();
    int fp = vm->fp_stack[vm->fp_count-1];
    int adjustment = vm->fp_count == 0 ? -1 : 0;
    push(vm, AS_PTR(&vm->stack[fp+adjustment+idx]));
    return 0;
}

static inline int handle_op_addr_global(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    uint32_t name_idx = READ_UINT32();
    Object *obj = table_get(&vm->globals, chunk->sp.data[name_idx]);
    push(vm, AS_PTR(obj));
    return 0;
}

static inline int handle_op_deref(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    Object ptr = pop(vm);
    push(vm, *ptr.as.ptr);
    OBJECT_INCREF(*ptr.as.ptr);
    return 0;
}

typedef int (*HandlerFn)(VM *vm, BytecodeChunk *chunk, uint8_t **ip);
typedef struct {
    HandlerFn fn;
    char *opcode;
} Handler;

Handler dispatcher[] = {
    [OP_PRINT] = { .fn = handle_op_print, .opcode = "OP_PRINT" },
    [OP_CONST] = { .fn = handle_op_const, .opcode = "OP_CONST" },
    [OP_GET_GLOBAL] = { .fn = handle_op_get_global, .opcode = "OP_GET_GLOBAL" },
    [OP_SET_GLOBAL] = { .fn = handle_op_set_global, .opcode = "OP_SET_GLOBAL" },
    [OP_SET_GLOBAL_DEREF] = { .fn = handle_op_set_global_deref, .opcode = "OP_SET_GLOBAL_DEREF" },
    [OP_STR] = { .fn = handle_op_str, .opcode = "OP_STR" },
    [OP_DEEPGET] = { .fn = handle_op_deepget, .opcode = "OP_DEEPGET" },
    [OP_DEEPSET] = { .fn = handle_op_deepset, .opcode = "OP_DEEPSET" },
    [OP_DEEPSET_DEREF] = { .fn = handle_op_deepset_deref, .opcode = "OP_DEEPSET_DEREF" },
    [OP_GETATTR] = { .fn = handle_op_getattr, .opcode = "OP_GETATTR" },
    [OP_SETATTR] = { .fn = handle_op_setattr, .opcode = "OP_SETATTR" },
    [OP_ADD] = { .fn = handle_op_add, .opcode = "OP_ADD" },
    [OP_SUB] = { .fn = handle_op_sub, .opcode = "OP_SUB" },
    [OP_MUL] = { .fn = handle_op_mul, .opcode = "OP_MUL" },
    [OP_DIV] = { .fn = handle_op_div, .opcode = "OP_DIV" },
    [OP_MOD] = { .fn = handle_op_mod, .opcode = "OP_MOD" },
    [OP_GT] = { .fn = handle_op_gt, .opcode = "OP_GT" },
    [OP_LT] = { .fn = handle_op_lt, .opcode = "OP_LT" },
    [OP_EQ] = { .fn = handle_op_eq, .opcode = "OP_EQ" },
    [OP_JZ] = { .fn = handle_op_jz, .opcode = "OP_JZ" },
    [OP_JMP] = { .fn = handle_op_jmp, .opcode = "OP_JMP" },
    [OP_NEG] = { .fn = handle_op_neg, .opcode = "OP_NEG" },
    [OP_NOT] = { .fn = handle_op_not, .opcode = "OP_NOT" },
    [OP_RET] = { .fn = handle_op_ret, .opcode = "OP_RET" },
    [OP_TRUE] = { .fn = handle_op_true, .opcode = "OP_TRUE" },
    [OP_NULL] = { .fn = handle_op_null, .opcode = "OP_NULL" },
    [OP_STRUCT] = { .fn = handle_op_struct, .opcode = "OP_STRUCT" },
    [OP_IP] = { .fn = handle_op_ip, .opcode = "OP_IP" },
    [OP_INC_FPCOUNT] = { .fn = handle_op_inc_fpcount, .opcode = "OP_INC_FPCOUNT" },
    [OP_POP] = { .fn = handle_op_pop, .opcode = "OP_POP" },
    [OP_ADDR] = { .fn = handle_op_addr, .opcode = "OP_ADDR" },
    [OP_ADDR_GLOBAL] = { .fn = handle_op_addr_global, .opcode = "OP_ADDR_GLOBAL" },
    [OP_DEREF] = { .fn = handle_op_deref, .opcode = "OP_DEREF" },
};

void print_current_instruction(uint8_t *ip) {
    printf("current instruction: %s\n", dispatcher[*ip].opcode);
}

int run(VM *vm, BytecodeChunk *chunk) {
#ifdef venom_debug_disassembler
    disassemble(chunk);
#endif

    for (
        uint8_t *ip = chunk->code.data;
        ip < &chunk->code.data[chunk->code.count];  /* ip < addr of just beyond the last instruction */
        ip++
    ) {
#ifdef venom_debug_vm
        print_current_instruction(ip);
#endif
        int status = dispatcher[*ip].fn(vm, chunk, &ip);
        if (status != 0) return status;
#ifdef venom_debug_vm
        PRINT_STACK();
#endif
    }

    return 0;
}