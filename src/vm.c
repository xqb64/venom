#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "math.h"
#include "disassembler.h"
#include "compiler.h"
#include "object.h"
#include "util.h"
#include "vm.h"

void init_vm(VM *vm) {
    memset(vm, 0, sizeof(VM));
}

void free_vm(VM* vm) {
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
    Object b = pop(vm); \
    Object a = pop(vm); \
    Object obj = wrapper(TO_DOUBLE(a) op TO_DOUBLE(b)); \
    push(vm, obj); \
} while (0)

#define READ_UINT8() (*++(*ip))

#define READ_INT16() \
    /* ip points to one of the jump instructions and \
     * there is a 2-byte operand (offset) that comes \
     * after the opcode. The instruction pointer ne- \
     * eds to be incremented to point to the last of \
     * the two operands, and a 16-bit offset constr- \
     * ucted from the two bytes. Then the instructi- \
     * on pointer will be incremented in by the main \
     * loop again and will point to the next instru- \
     * ction that comes after the jump. */           \
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
        return strcmp(TO_STR(*left)->value, TO_STR(*right)->value) == 0;
    }

    /* If both objects are structs, compare them. */
    if (IS_STRUCT(*left) && IS_STRUCT(*right)) {
        Struct *a = TO_STRUCT(*left);
        Struct *b = TO_STRUCT(*right);

        /* Return false if the structs are of different types. */
        if (strcmp(a->name, b->name) != 0) {
            return false;
        }

        /* If they have the same type, for each non-NULL
         * property in struct 'a', run the func recursi-
         * vely comparing that property with the corres-
         * ponding property in struct 'b'. */
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
        /* Comparing the properties didn't return false,
         * which means that the two structs are equal. */
        return true;
    }
    assert(0);
}

static inline uint32_t adjust_idx(VM *vm, uint32_t idx) {
    /* 'idx' is adjusted to be relative to the
     * current frame pointer ('adjustment' ta-
     * kes care of the case where there are no
     * frame pointers on the stack). */
    int fp = vm->fp_stack[vm->fp_count-1];
    int adjustment = vm->fp_count == 0 ? -1 : 0;
    return fp+adjustment+idx;
}

static inline Object *follow_ptr(Object *target, uint8_t deref_count) {
    /* 'target' is followed (and dereferenced) 'deref_count' times. */
    for (int i = 0; i < deref_count; i++) {
        target = target->as.ptr;
    }
    return target;
}

static inline String concatenate_strings(char *a, char *b) {
    int len_a = strlen(a);
    int len_b = strlen(b);
    int total_len = len_a + len_b + 1;
    char *result = malloc(total_len);
    strcpy(result, a);
    strcat(result, b);
    return (String){ .refcount = 1, .value = result };
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

static inline int handle_op_add(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    Object b = pop(vm);
    Object a = pop(vm);
    if (a.type == OBJ_NUMBER && b.type == OBJ_NUMBER) {
        push(vm, AS_DOUBLE(TO_DOUBLE(a) + TO_DOUBLE(b)));
    } else if (a.type == OBJ_STRING && b.type == OBJ_STRING) {
        String result = concatenate_strings(TO_STR(a)->value, TO_STR(b)->value);
        Object obj = AS_STR(ALLOC(result));
        push(vm, obj);
        OBJECT_DECREF(b);
        OBJECT_DECREF(a);
    } else {
        RUNTIME_ERROR(
            "'+' operator used on objects of unsupported types: %s and %s",
            GET_OBJTYPE(a.type),
            GET_OBJTYPE(b.type)
        );
    }
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

static inline int handle_op_gt(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(>, AS_BOOL);
    return 0;
}

static inline int handle_op_lt(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    BINARY_OP(<, AS_BOOL);
    return 0;
}

static inline int handle_op_not(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_NOT pops an object off the stack and pushes
     * its inverse back on the stack. The popped obj-
     * ect must be a boolean.  */
    Object obj = pop(vm);
    push(vm, AS_BOOL(TO_BOOL(obj) ^ 1));
    return 0;
}

static inline int handle_op_neg(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_NEG pops an object off the stack, negates
     * it and pushes the negative back on the stack. */
    Object original = pop(vm);
    Object negated = AS_DOUBLE(-TO_DOUBLE(original));
    push(vm, negated);
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

static inline int handle_op_str(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_STR reads a 4-byte index of the string in the ch-
     * unk's sp, constructs a string object with that value
     * and pushes it on the stack. */
    uint32_t idx = READ_UINT32();
    String s = { .refcount = 1, .value = own_string(chunk->sp.data[idx]) };
    Object obj = AS_STR(ALLOC(s));
    push(vm, obj);
    return 0;
}

static inline int handle_op_jz(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_JZ reads a signed 2-byte offset (that could be ne-
     * gative), pops an object off the stack, and increments
     * the instruction pointer by the offset, if and only if
     * the popped object was 'false'. */
    int16_t offset = READ_INT16();
    Object obj = pop(vm);
    if (!TO_BOOL(obj)) {
        *ip += offset;
    }
    return 0;
}

static inline int handle_op_jmp(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_JMP reads a signed 2-byte offset (that could be ne-
     * gative), and increments the instruction pointer by the
     * offset. Unlike OP_JZ, which is a conditional jump, the
     * OP_JMP instruction takes the jump unconditionally. */
    int16_t offset = READ_INT16();
    *ip += offset;
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
    /* OP_SET_GLOBAL_DEREF reads a 1-byte dereference count,
     * and a 4-byte index of the variable name in the sp. It
     * then gets the object with that name from the vm's gl-
     * obals table and follows the returned pointer (derefe-
     * rencing it on theway) 'deref_count' times, and final-
     * ly setting its value to the previously popped object. */
    uint8_t deref_count = READ_UINT8();
    uint32_t name_idx = READ_UINT32();
    Object obj = pop(vm);
    Object *target = follow_ptr(table_get(&vm->globals, chunk->sp.data[name_idx]), deref_count);
    *target = obj;
    return 0;
}

static inline int handle_op_get_global(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_GET_GLOBAL reads a 4-byte index of the variable name
     * in the chunk's sp, looks up an object with that name in
     * the vm's globals table, and pushes it on the stack. Si-
     * nce the object will be present in yet another location,
     * the refcount must be incremented. */
    uint32_t name_idx = READ_UINT32();
    Object *obj = table_get(&vm->globals, chunk->sp.data[name_idx]);
    push(vm, *obj);
    OBJECT_INCREF(*obj);
    return 0;
}

static inline int handle_op_get_global_ptr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_GET_GLOBAL_PTR reads a 4-byte index of the variable
     * name in the chunk's sp, looks up the object under that
     * name in in the vm's globals table, and pushes its add-
     * ress on the stack. */
    uint32_t name_idx = READ_UINT32();
    Object *obj = table_get(&vm->globals, chunk->sp.data[name_idx]);
    push(vm, AS_PTR(obj));
    return 0;
}

static inline int handle_op_deepset(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_DEEPSET reads a 4-byte index (1-based) of the obj-
     * ect being modified, which is adjusted and used to set
     * the object in that position to the popped object.
     *
     * Considering that the object being set will be overwr-
     * itten, its reference count must be decremented before
     * putting the popped object into that position.
     * 
     * For example, if there is a variable 'thing', which is
     * a number object with value 2:
     * 
     *      [fp, 1, 2, 3, ..., 8]
     * 
     * ...and if the user sets it to, let's say, 8, by doing
     * 
     *      thing = 8;
     * 
     * ...the stack will look like below:
     * 
     *      [fp, 1, 8, 3, ...]
     */ 
    uint32_t idx = READ_UINT32();
    uint32_t adjusted_idx = adjust_idx(vm, idx);
    Object obj = pop(vm);
    OBJECT_DECREF(vm->stack[adjusted_idx]);
    vm->stack[adjusted_idx] = obj;
    return 0;
}

static inline int handle_op_deepset_deref(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_DEEPSET_DEREF reads a 1-byte dereference count and
     * a 4-byte index (1-based) of the object being modified
     * (which must be a pointer). The adjusted index is used
     * to follow the pointer in that position, and set it to
     * the previously popped object.
     *
     * Considering that the object being set will be overwr-
     * itten, its reference count must be decremented before
     * putting the popped object into that position.
     * 
     * For example, if there is a variable 'thing', which is
     * a pointer to pointer to pointer to some variable (e.g.
     * a boolean, false):
     * 
     *      [ptr, ..., true]
     *        ↓
     *       ptr
     *        ↓
     *       ptr
     *        ↓
     *      false
     * 
     * ...and the user does:
     * 
     *      ***thing = true;
     * 
     * ...it will follow the first ptr on the stack until it
     * reaches the last ptr (the one that points to 'false')
     * and change its value to 'true'. */
    uint8_t deref_count = READ_UINT8();
    uint32_t idx = READ_UINT32();
    uint32_t adjusted_idx = adjust_idx(vm, idx);
    Object obj = pop(vm);
    Object *target = follow_ptr(&vm->stack[adjusted_idx], deref_count);
    OBJECT_DECREF(*target);
    *target = obj;
    return 0;
}

static inline int handle_op_deepget(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_DEEPGET reads a 4-byte index (1-based) of the obj-
     * ect being accessed, which is adjusted and used to get
     * the object in that position and push it on the stack.
     *
     * Since the object being accessed will now be available
     * in yet another location, its refcount must be increm-
     * ented.
     * 
     * For example, if there is a variable 'thing', which is
     * a number object with value 2:
     * 
     *      [fp, 1, 2, 3, ...]
     * 
     * ...and if the user references 'thing' in an expressi-
     * on, the stack will look like below:
     * 
     *      [fp, 1, 2, 3, ..., 2]
     */   
    uint32_t idx = READ_UINT32();
    uint32_t adjusted_idx = adjust_idx(vm, idx);
    Object obj = vm->stack[adjusted_idx];
    push(vm, obj);
    OBJECT_INCREF(obj);
    return 0;
}

static inline int handle_op_deepget_ptr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_DEEPGET_PTR reads a 4-byte index (1-based) of the
     * object being accessed, which is adjusted and used to
     * access the object in that position and push its add-
     * ress on the stack. */
    uint32_t idx = READ_UINT32();
    uint32_t adjusted_idx = adjust_idx(vm, idx);
    push(vm, AS_PTR(&vm->stack[adjusted_idx]));
    return 0;
}

static inline int handle_op_setattr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_SETATTR reads a 4-byte index of the property name in
     * the chunk's sp, pops two objects off the stack (a value
     * of the property, and the object being modified) and in-
     * serts the value into the object's properties Table. Th-
     * en it pushes the modified object back on the stack. */
    uint32_t property_name_idx = READ_UINT32();
    Object value = pop(vm);
    Object structobj = pop(vm);
    table_insert(TO_STRUCT(structobj)->properties, chunk->sp.data[property_name_idx], value);
    push(vm, structobj);
    return 0;
}

static inline int handle_op_getattr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_GETATTR reads a 4-byte index of the property name in
     * the sp. Then, it pops an object off the stack, and loo-
     * ks up the property with that name in its properties Ta-
     * ble. If the property is found, it will be pushed on the
     * stack. Otherwise, a runtime error is raised. */
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
    push(vm, *property);
    OBJECT_INCREF(*property);
    OBJECT_DECREF(obj);
    return 0;
}

static inline int handle_op_getattr_ptr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_GETATTR_PTR reads a 4-byte index of the property name
     * in the chunk's sp. Then, it pops an object off the stack
     * and looks up the property with that name in the object's
     * properties Table. If the property is found, a pointer to
     * it is pushed on the stack (conveniently, table_get() re-
     * turns a pointer). Otherwise, a runtime error is raised. */
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
    push(vm, AS_PTR(property));
    OBJECT_DECREF(obj);
    return 0;
}

static inline int handle_op_struct(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_STRUCT reads a 4-byte index of the struct name in the
     * sp, constructs a struct object with that name and refco-
     * unt set to 1 (while making sure to initialize the prope-
     * rties table properly), and pushes it on the stack. */
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

static inline int handle_op_ip(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_IP reads a signed 2-byte offset and constructs a by-
     * tecode pointer object which points to the next instruc-
     * tion that comes after the function call dance, and pus-
     * hes it on the stack. Besides this, it updates the frame
     * pointer stack.
     *
     * NOTE: vm->fp_stack should be updated first before push-
     * ing the bytecode pointer on the stack, because the ins-
     * tructions that base their indexing off of frame pointe-
     * rs expect the indexes to be zero-based. */
    int16_t offset = READ_INT16();
    Object ip_obj = AS_BCPTR(*(ip)+offset);
    vm->fp_stack[vm->fp_count] = vm->tos;
    push(vm, ip_obj);
    return 0;
}

static inline int handle_op_inc_fpcount(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_INC_FPCOUNT increments vm->fp_count, effectively co-
     * mpleting the entire function call dance. */
    vm->fp_count++;
    return 0;
}

static inline int handle_op_ret(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_RET pops the return value and the return address off
     * the stack (because the return address, which is what is
     * actually needed, is located beneath the return value on
     * the stack), and modifies the instruction pointer to po-
     * int to the return address. Besides, it ends the functi-
     * on call by decrementing vm->fp_count and making sure to
     * put the return value back on the stack.   */
    Object returnvalue = pop(vm);
    Object returnaddr = pop(vm);
    --vm->fp_count;
    push(vm, returnvalue);
    *ip = returnaddr.as.bcptr;
    return 0;
}

static inline int handle_op_pop(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_POP pops an object off the stack. Since the popped
     * object might be refcounted, its refcount must be dec-
     * remented. */
    Object obj = pop(vm);
    OBJECT_DECREF(obj);
    return 0;
}

static inline int handle_op_deref(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
    /* OP_DEREF pops an object off the stack, dereferences it
     * and pushes it back on the stack. Since the object will
     * be present in one more another location, its reference
     * count must be incremented.*/
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
    [OP_ADD] = { .fn = handle_op_add, .opcode = "OP_ADD" },
    [OP_SUB] = { .fn = handle_op_sub, .opcode = "OP_SUB" },
    [OP_MUL] = { .fn = handle_op_mul, .opcode = "OP_MUL" },
    [OP_DIV] = { .fn = handle_op_div, .opcode = "OP_DIV" },
    [OP_MOD] = { .fn = handle_op_mod, .opcode = "OP_MOD" },
    [OP_EQ] = { .fn = handle_op_eq, .opcode = "OP_EQ" },
    [OP_GT] = { .fn = handle_op_gt, .opcode = "OP_GT" },
    [OP_LT] = { .fn = handle_op_lt, .opcode = "OP_LT" },
    [OP_NOT] = { .fn = handle_op_not, .opcode = "OP_NOT" },
    [OP_NEG] = { .fn = handle_op_neg, .opcode = "OP_NEG" },
    [OP_TRUE] = { .fn = handle_op_true, .opcode = "OP_TRUE" },
    [OP_NULL] = { .fn = handle_op_null, .opcode = "OP_NULL" },
    [OP_CONST] = { .fn = handle_op_const, .opcode = "OP_CONST" },
    [OP_STR] = { .fn = handle_op_str, .opcode = "OP_STR" },
    [OP_JZ] = { .fn = handle_op_jz, .opcode = "OP_JZ" },
    [OP_JMP] = { .fn = handle_op_jmp, .opcode = "OP_JMP" },
    [OP_SET_GLOBAL] = { .fn = handle_op_set_global, .opcode = "OP_SET_GLOBAL" },
    [OP_SET_GLOBAL_DEREF] = { .fn = handle_op_set_global_deref, .opcode = "OP_SET_GLOBAL_DEREF" },
    [OP_GET_GLOBAL] = { .fn = handle_op_get_global, .opcode = "OP_GET_GLOBAL" },
    [OP_GET_GLOBAL_PTR] = { .fn = handle_op_get_global_ptr, .opcode = "OP_GET_GLOBAL_PTR" },
    [OP_DEEPSET] = { .fn = handle_op_deepset, .opcode = "OP_DEEPSET" },
    [OP_DEEPSET_DEREF] = { .fn = handle_op_deepset_deref, .opcode = "OP_DEEPSET_DEREF" },
    [OP_DEEPGET] = { .fn = handle_op_deepget, .opcode = "OP_DEEPGET" },
    [OP_DEEPGET_PTR] = { .fn = handle_op_deepget_ptr, .opcode = "OP_DEEPGET_PTR" },
    [OP_SETATTR] = { .fn = handle_op_setattr, .opcode = "OP_SETATTR" },
    [OP_GETATTR] = { .fn = handle_op_getattr, .opcode = "OP_GETATTR" },
    [OP_GETATTR_PTR] = { .fn = handle_op_getattr_ptr, .opcode = "OP_GETATTR_PTR" },
    [OP_STRUCT] = { .fn = handle_op_struct, .opcode = "OP_STRUCT" },
    [OP_IP] = { .fn = handle_op_ip, .opcode = "OP_IP" },
    [OP_INC_FPCOUNT] = { .fn = handle_op_inc_fpcount, .opcode = "OP_INC_FPCOUNT" },
    [OP_RET] = { .fn = handle_op_ret, .opcode = "OP_RET" },
    [OP_POP] = { .fn = handle_op_pop, .opcode = "OP_POP" },
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