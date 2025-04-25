#include "vm.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "compiler.h"
#include "dynarray.h"
#include "math.h"
#include "object.h"
#include "table.h"
#include "util.h"

void init_vm(VM *vm)
{
    memset(vm, 0, sizeof(VM));
    vm->blueprints = calloc(1, sizeof(Table_StructBlueprint));
}

void free_vm(VM *vm)
{
    free_table_object(&vm->globals);
    free_table_struct_blueprints(vm->blueprints);
    free(vm->blueprints);
}

static inline void push(VM *vm, Object obj)
{
    vm->stack[vm->tos++] = obj;
}

static inline Object pop(VM *vm)
{
    return vm->stack[--vm->tos];
}

static inline Object peek(VM *vm, int n)
{
    return vm->stack[vm->tos - 1 - n];
}

static inline uint64_t clamp(double d)
{
    if (d < 0.0)
        return 0;
    else if (d > UINT64_MAX)
        return UINT64_MAX;
    else
        return (uint64_t) d;
}

#define BINARY_OP(op, wrapper)                        \
    do                                                \
    {                                                 \
        Object b = pop(vm);                           \
        Object a = pop(vm);                           \
        Object obj = wrapper(AS_NUM(a) op AS_NUM(b)); \
        push(vm, obj);                                \
    } while (0)

#define BITWISE_OP(op)                            \
    do                                            \
    {                                             \
        Object b = pop(vm);                       \
        Object a = pop(vm);                       \
                                                  \
        uint64_t clamped_a = clamp(AS_NUM(a));    \
        uint64_t clamped_b = clamp(AS_NUM(b));    \
                                                  \
        uint64_t result = clamped_a op clamped_b; \
                                                  \
        Object obj = NUM_VAL((double) result);    \
                                                  \
        push(vm, obj);                            \
    } while (0)

#define READ_UINT8() (*++(*ip))

#define READ_INT16()                                 \
    /* ip points to one of the jump instructions and \
     * there is a 2-byte operand (offset) that comes \
     * after the opcode. The instruction pointer ne- \
     * eds to be incremented to point to the last of \
     * the two operands, and a 16-bit offset constr- \
     * ucted from the two bytes. Then the ip will be \
     * incremented by the mainloop again to point to \
     * the next opcode that comes after the jump. */ \
    (*ip += 2, (int16_t) (((*ip)[-1] << 8) | (*ip)[0]))

#define READ_UINT32() \
    (*ip += 4, (uint32_t) (((*ip)[-3] << 24) | ((*ip)[-2] << 16) | ((*ip)[-1] << 8) | (*ip)[0]))

#define READ_DOUBLE()                                                                              \
    (*ip += 8,                                                                                     \
     (((uint64_t) (*ip)[-7] << 56) | ((uint64_t) (*ip)[-6] << 48) | ((uint64_t) (*ip)[-5] << 40) | \
      ((uint64_t) (*ip)[-4] << 32) | ((uint64_t) (*ip)[-3] << 24) | ((uint64_t) (*ip)[-2] << 16) | \
      ((uint64_t) (*ip)[-1] << 8) | (uint64_t) (*ip)[0]))

#define PRINT_STACK()                        \
    do                                       \
    {                                        \
        printf("stack: [");                  \
        for (size_t i = 0; i < vm->tos; i++) \
        {                                    \
            print_object(&vm->stack[i]);     \
            if (i < vm->tos - 1)             \
            {                                \
                printf(", ");                \
            }                                \
        }                                    \
        printf("]\n");                       \
    } while (0)

#define PRINT_FPSTACK()                                                                         \
    do                                                                                          \
    {                                                                                           \
        printf("fp stack: [");                                                                  \
        for (size_t i = 0; i < vm->fp_count; i++)                                               \
        {                                                                                       \
            printf("<%s (loc: %d)>", vm->fp_stack[i].fn->func->name, vm->fp_stack[i].location); \
            if (i < vm->fp_count - 1)                                                           \
            {                                                                                   \
                printf(", ");                                                                   \
            }                                                                                   \
        }                                                                                       \
        printf("]\n");                                                                          \
    } while (0)

#define RUNTIME_ERROR(...)            \
    do                                \
    {                                 \
        fprintf(stderr, "vm: ");      \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
        exit(1);                      \
    } while (0)

static inline bool check_equality(Object *left, Object *right)
{
#ifdef NAN_BOXING
    if (IS_NUM(*left) && IS_NUM(*right))
    {
        return AS_NUM(*left) == AS_NUM(*right);
    }
    else if (IS_STRING(*left) && IS_STRING(*right))
    {
        return strcmp(AS_STRING(*left)->value, AS_STRING(*right)->value) == 0;
    }
    return *left == *right;
#else

    /* Return false if the objects are of different type. */
    if (left->type != right->type)
    {
        return false;
    }

    switch (left->type)
    {
        case OBJ_STRING: {
            return strcmp(AS_STRING(*left)->value, AS_STRING(*right)->value) == 0;
        }
        case OBJ_STRUCT: {
            return AS_STRUCT(*left) == AS_STRUCT(*right);
        }
        case OBJ_NULL: {
            return true;
        }
        case OBJ_BOOLEAN: {
            return AS_BOOL(*left) == AS_BOOL(*right);
        }
        case OBJ_NUMBER: {
            return AS_NUM(*left) == AS_NUM(*right);
        }
        default:
            assert(0);
    }
#endif
}

static inline uint32_t adjust_idx(VM *vm, uint32_t idx)
{
    /* 'idx' is adjusted to be relative to the current fra-
     * me pointer, if there are any. If not, it is returned
     * back, as is. */
    if (vm->fp_count > 0)
    {
        BytecodePtr fp = vm->fp_stack[vm->fp_count - 1];
        return fp.location + idx;
    }
    else
    {
        return idx;
    }
}

static inline char *concatenate_strings(char *a, char *b)
{
    int len_a = strlen(a);
    int len_b = strlen(b);
    int total_len = len_a + len_b + 1;
    char *result = malloc(total_len);
    strcpy(result, a);
    strcat(result, b);
    return result;
}

/* OP_PRINT pops an object off the stack and prints it,
 * prefixing it with "dbg print :: " in debug=vm mode.
 *
 * REFCOUNTING: Since the popped object might be refco-
 * unted, the reference count must be decremented. */
static inline void handle_op_print(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object object = pop(vm);

#ifdef venom_debug_vm
    printf("dbg print :: ");
#endif

    print_object(&object);
    printf("\n");

    objdecref(&object);
}

/* OP_ADD pops two objects off the stack, adds them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_add(VM *vm, Bytecode *code, uint8_t **ip)
{
    BINARY_OP(+, NUM_VAL);
}

/* OP_SUB pops two objects off the stack, subs them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_sub(VM *vm, Bytecode *code, uint8_t **ip)
{
    BINARY_OP(-, NUM_VAL);
}

/* OP_MUL pops two objects off the stack, muls them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_mul(VM *vm, Bytecode *code, uint8_t **ip)
{
    BINARY_OP(*, NUM_VAL);
}

/* OP_DIV pops two objects off the stack, divs them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_div(VM *vm, Bytecode *code, uint8_t **ip)
{
    BINARY_OP(/, NUM_VAL);
}

/* OP_MOD pops two objects off the stack, mods them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_mod(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object b = pop(vm);
    Object a = pop(vm);

    Object obj = NUM_VAL(fmod(AS_NUM(a), AS_NUM(b)));

    push(vm, obj);
}

/* OP_BITAND pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise AND operati-
 * on on them, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_bitand(VM *vm, Bytecode *code, uint8_t **ip)
{
    BITWISE_OP(&);
}

/* OP_BITOR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise OR operati-
 * on on them, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_bitor(VM *vm, Bytecode *code, uint8_t **ip)
{
    BITWISE_OP(|);
}

/* OP_BITXOR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise XOR operati-
 * on on them, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_bitxor(VM *vm, Bytecode *code, uint8_t **ip)
{
    BITWISE_OP(^);
}

/* OP_BITNOT pops an object off the stack, clamps it to
 * to [0, UINT64_MAX], performs the bitwise NOT operat-
 * ion on it, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the object is a
 * number because this handler does not do a runtime type
 * check. */
static inline void handle_op_bitnot(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object obj = pop(vm);

    uint64_t clamped = clamp(AS_NUM(obj));

    uint64_t inverted = ~clamped;

    push(vm, NUM_VAL(inverted));
}

/* OP_BITSHL pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise SHL operati-
 * on on them, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_bitshl(VM *vm, Bytecode *code, uint8_t **ip)
{
    BITWISE_OP(<<);
}

/* OP_BITSHR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise SHR operati-
 * on on them, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_bitshr(VM *vm, Bytecode *code, uint8_t **ip)
{
    BITWISE_OP(>>);
}

/* OP_EQ pops two objects off the stack, clamps them to
 * [0, UINT64_MAX], performs the equality check on them
 * and pushes the result on the stack.
 *
 * REFCOUNTING: Since the two objects might be refcoun-
 * ted, the reference count for both must be decrement-
 * ed.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are bools, because this handler does not do runti-
 * me type checks. */
static inline void handle_op_eq(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object b = pop(vm);
    Object a = pop(vm);

    objdecref(&a);
    objdecref(&b);

    push(vm, BOOL_VAL(check_equality(&a, &b)));
}

/* OP_GT pops two objects off the stack, compares them us-
 * ing the GT operation, and pushes the result back on the
 * stack.
 *
 * SAFETY: It is up to the user to ensure the two objects
 * are numbers, because this handler does not do runtime
 * type checks. */
static inline void handle_op_gt(VM *vm, Bytecode *code, uint8_t **ip)
{
    BINARY_OP(>, BOOL_VAL);
}

/* OP_LT pops two objects off the stack, compares them us-
 * ing the LT operation, and pushes the result back on the
 * stack.
 *
 * SAFETY: It is up to the user to ensure the two objects
 * are numbers, because this handler does not do runtime
 * type checks. */
static inline void handle_op_lt(VM *vm, Bytecode *code, uint8_t **ip)
{
    BINARY_OP(<, BOOL_VAL);
}

/* OP_NOT pops an object off the stack, performs the
 * logical NOT operation on it by inverting its bool
 * value, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the object
 * is a bool, because this handler does not do a ru-
 * ntime type check. */
static inline void handle_op_not(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object obj = pop(vm);
    push(vm, BOOL_VAL(AS_BOOL(obj) ^ 1));
}

/* OP_NEG pops an object off the stack, performs the
 * logical NEG operation on it by negating its value,
 * and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the object is
 * a number, because this handler does not do a runtime
 * type check. */
static inline void handle_op_neg(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object original = pop(vm);
    Object negated = NUM_VAL(-AS_NUM(original));
    push(vm, negated);
}

/* OP_TRUE pushes a bool object ('true') on the stack. */
static inline void handle_op_true(VM *vm, Bytecode *code, uint8_t **ip)
{
    push(vm, BOOL_VAL(true));
}

/* OP_NULL pushes a null object on the stack. */
static inline void handle_op_null(VM *vm, Bytecode *code, uint8_t **ip)
{
    push(vm, NULL_VAL);
}

/* OP_CONST reads a 4-byte index of the constant in the
 * chunk's cp, constructs an object with that value and
 * pushes it on the stack. */
static inline void handle_op_const(VM *vm, Bytecode *code, uint8_t **ip)
{
    union {
        double d;
        uint64_t raw;
    } num;
    num.raw = READ_DOUBLE();
    push(vm, NUM_VAL(num.d));
}

/* OP_STR reads a 4-byte index of the string in the ch-
 * unk's sp, constructs a string object with that value
 * and pushes it on the stack.
 *
 * REFCOUNTING: Since Strings are refcounted, the newly
 * constructed object has a refcount=1. */
static inline void handle_op_str(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t idx = READ_UINT32();

    String s = {.refcount = 1, .value = own_string(code->sp.data[idx])};

    push(vm, STRING_VAL(ALLOC(s)));
}

/* OP_JZ reads a signed 2-byte offset (that could be ne-
 * gative), pops an object off the stack, and increments
 * the instruction pointer by the offset, if and only if
 * the popped object was 'false'. */
static inline void handle_op_jz(VM *vm, Bytecode *code, uint8_t **ip)
{
    int16_t offset = READ_INT16();
    Object obj = pop(vm);
    if (!AS_BOOL(obj))
    {
        *ip += offset;
    }
}

/* OP_JMP reads a signed 2-byte offset (that could be ne-
 * gative), and increments the instruction pointer by the
 * offset. Unlike OP_JZ, which is a conditional jump, the
 * OP_JMP instruction takes the jump unconditionally. */
static inline void handle_op_jmp(VM *vm, Bytecode *code, uint8_t **ip)
{
    int16_t offset = READ_INT16();
    *ip += offset;
}

/* OP_SET_GLOBAL reads a 4-byte index of the variable name
 * in the chunk's sp, pops an object off the stack and in-
 * serts it into the vm's globals table under that name.
 *
 * REFCOUNTING: We do NOT need to increment the refcount of
 * the object we are inserting into the table because we're
 * merely moving it from one location to another.
 *
 * REFCOUNTING: However, we /DO/ need to decrement the ref-
 * fcount of the target, in case we're overwriting an obje-
 * ct with the same name. Don't ask me how I learned this. ;-) */
static inline void handle_op_set_global(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t name_idx = READ_UINT32();
    Object obj = pop(vm);

    Object *target = table_get(&vm->globals, code->sp.data[name_idx]);
    if (target)
        objdecref(target);

    table_insert(&vm->globals, code->sp.data[name_idx], obj);
}

/* OP_GET_GLOBAL reads a 4-byte index of the variable name
 * in the chunk's sp, looks up an object with that name in
 * the vm's globals table, and pushes it on the stack.
 *
 * REFCOUNTING: Since the object will be present in yet an-
 * other location, the refcount must be incremented. */
static inline void handle_op_get_global(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t name_idx = READ_UINT32();
    Object *obj = table_get_unchecked(&vm->globals, code->sp.data[name_idx]);
    push(vm, *obj);
    objincref(obj);
}

/* OP_GET_GLOBAL_PTR reads a 4-byte index of the variable
 * name in the chunk's sp, looks up the object under that
 * name in in the vm's globals table, and pushes its add-
 * ress on the stack. */
static inline void handle_op_get_global_ptr(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t name_idx = READ_UINT32();
    Object *object_ptr = table_get_unchecked(&vm->globals, code->sp.data[name_idx]);
    push(vm, PTR_VAL(object_ptr));
}

/* OP_DEEPSET reads a 4-byte index (1-based) of the obj-
 * ect being modified, which is adjusted and used to set
 * the object in that position to the popped object.
 *
 * REFCOUNTING: Since the object being set will be over-
 * written, its reference count must be decremented bef-
 * ore putting the popped object into that position. */
static inline void handle_op_deepset(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t idx = READ_UINT32();
    uint32_t adjusted_idx = adjust_idx(vm, idx);
    Object obj = pop(vm);
    objdecref(&vm->stack[adjusted_idx]);
    vm->stack[adjusted_idx] = obj;
}

/* OP_DEREFSET pops two objects off the stack which are
 * expected to be:
 * - the value that is being assigned
 * - the assignee (a pointer)
 * It then dereferences the pointer and sets the value,
 * essentially changing what the pointer points to.
 *
 * REFCOUNTING: We do NOT need to incref/decref the obj-
 * ect here because we're merely moving it from one loc-
 * ation to another. */
static inline void handle_op_derefset(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object item = pop(vm);
    Object ptr = pop(vm);

    *AS_PTR(ptr) = item;
}

/* OP_DEEPGET reads a 4-byte index (1-based) of the obj-
 * ect being accessed, which is adjusted and used to get
 * the object in that position and push it on the stack.
 *
 * REFCOUNTING: Since the object being accessed will now
 * be available in yet another location, we need to inc-
 * rement its refcount. */
static inline void handle_op_deepget(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t idx = READ_UINT32();
    uint32_t adjusted_idx = adjust_idx(vm, idx);
    Object obj = vm->stack[adjusted_idx];
    push(vm, obj);
    objincref(&obj);
}

/* OP_DEEPGET_PTR reads a 4-byte index (1-based) of the
 * object being accessed, which is adjusted and used to
 * access the object in that position and push its add-
 * ress on the stack. */
static inline void handle_op_deepget_ptr(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t idx = READ_UINT32();
    uint32_t adjusted_idx = adjust_idx(vm, idx);
    Object *object_ptr = &vm->stack[adjusted_idx];
    push(vm, PTR_VAL(object_ptr));
}

/* OP_SETATTR reads a 4-byte index of the property name in
 * the chunk's sp, pops two objects off the stack (a value
 * of the property, and the object being modified) and in-
 * serts the value into the object's properties Table. Th-
 * en it pushes the modified object back on the stack.
 *
 * SAFETY: The handler won't try to ensure that the acces-
 * sed property is defined on the object being modified.
 *
 * REFCOUNTING: If the modified attribute already exists, we
 * need to make sure to decrement the refcount before overw-
 * riting it. */
static inline void handle_op_setattr(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t property_name_idx = READ_UINT32();

    Object value = pop(vm);
    Object obj = pop(vm);

    Object *target = table_get(AS_STRUCT(obj)->properties, code->sp.data[property_name_idx]);
    if (target)
    {
        objdecref(target);
    }

    table_insert(AS_STRUCT(obj)->properties, code->sp.data[property_name_idx], value);

    push(vm, obj);
}

/* OP_GETATTR reads a 4-byte index of the property name in
 * the sp. Then, it pops an object off the stack, and loo-
 * ks up the property with that name in its properties Ta-
 * ble. If the property is found, it will be pushed on the
 * stack. Otherwise, a runtime error is raised.
 *
 * REFCOUNTING:
 *
 * Since the property will now be present in yet another
 * location, its recount must be incremented.
 *
 * Since the popped object will no longer present at the
 * location, its refcount must be decremented. */
static inline void handle_op_getattr(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t property_name_idx = READ_UINT32();
    Object obj = pop(vm);

    Object *property = table_get(AS_STRUCT(obj)->properties, code->sp.data[property_name_idx]);

    if (!property)
    {
        RUNTIME_ERROR("Property '%s' is not defined on struct '%s'.",
                      code->sp.data[property_name_idx], AS_STRUCT(obj)->name);
    }

    push(vm, *property);
    objincref(property);
    objdecref(&obj);
}

/* OP_GETATTR_PTR reads a 4-byte index of the property name
 * in the chunk's sp. Then, it pops an object off the stack
 * and looks up the property with that name in the object's
 * properties Table. If the property is found, a pointer to
 * it is pushed on the stack (conveniently, table_get() re-
 * turns a pointer). Otherwise, a runtime error is raised.
 *
 * REFCOUNTING: Since the popped object will no longer pre-
 * sent at that location, its refcount must be decremented. */
static inline void handle_op_getattr_ptr(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t property_name_idx = READ_UINT32();
    Object object = pop(vm);

    StructBlueprint *sb = table_get_unchecked(vm->blueprints, AS_STRUCT(object)->name);
    int *idx = table_get(sb->property_indexes, code->sp.data[property_name_idx]);
    if (!idx)
    {
        RUNTIME_ERROR("struct '%s' does not have property '%s'", AS_STRUCT(object)->name,
                      code->sp.data[property_name_idx]);
    }

    Object *property = table_get(AS_STRUCT(object)->properties, code->sp.data[property_name_idx]);
    push(vm, PTR_VAL(property));

    objdecref(&object);
}

/* OP_STRUCT reads a 4-byte index of the struct name in the
 * sp, constructs a struct object with that name and refco-
 * unt set to 1 (while making sure to initialize the prope-
 * rties table properly), and pushes it on the stack.
 *
 * REFCOUNTING: Since Structs are refcounted, the newly co-
 * nstructed object has a refcount=1. */
static inline void handle_op_struct(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t structname = READ_UINT32();

    StructBlueprint *sb = table_get(vm->blueprints, code->sp.data[structname]);
    if (!sb)
    {
        RUNTIME_ERROR("struct '%s' is not defined", code->sp.data[structname]);
    }

    Struct s = {.name = code->sp.data[structname],
                .propcount = sb->property_indexes->count,
                .refcount = 1,
                .properties = calloc(1, sizeof(Table_Object))};

    for (size_t i = 0; i < sb->methods->count; i++)
    {
        Function f = {
            .location = sb->methods->items[i]->location,
            .name = sb->methods->items[i]->name,
            .paramcount = sb->methods->items[i]->paramcount,
            .upvalue_count = 0,
        };

        Closure c = {.func = ALLOC(f), .refcount = 1, .upvalue_count = 0, .upvalues = NULL};

        table_insert(s.properties, sb->methods->items[i]->name, CLOSURE_VAL(ALLOC(c)));
    }

    push(vm, STRUCT_VAL(ALLOC(s)));
}

/* OP_STRUCT_BLUEPRINT reads a 4-byte name index of the
 * struct name in the sp, then it reads a 4-byte prope-
 * rty count of the said struct (let's call this propc-
 * ount). Then, it loops 'propcount' times and for each
 * property, it reads the name index in the sp, and pr-
 * operty index (in the items array in the Table_int of
 * the StructBlueprint). Finally, it uses all this info
 * to construct a StructBlueprint object, initialize it
 * properly, and insert it into the vm's blueprints ta-
 * ble. */
static inline void handle_op_struct_blueprint(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t name_idx = READ_UINT32();
    uint32_t propcount = READ_UINT32();

    DynArray_char_ptr properties = {0};
    DynArray_uint32_t prop_indexes = {0};
    for (size_t i = 0; i < propcount; i++)
    {
        dynarray_insert(&properties, code->sp.data[READ_UINT32()]);
        dynarray_insert(&prop_indexes, READ_UINT32());
    }

    StructBlueprint sb = {.name = code->sp.data[name_idx],
                          .property_indexes = calloc(1, sizeof(Table_int)),
                          .methods = calloc(1, sizeof(Table_Function))};

    for (size_t i = 0; i < properties.count; i++)
    {
        table_insert(sb.property_indexes, properties.data[i], prop_indexes.data[i]);
    }

    table_insert(vm->blueprints, code->sp.data[name_idx], sb);

    dynarray_free(&properties);
    dynarray_free(&prop_indexes);
}

/* OP_IMPL reads a 4-byte blueprint name idx in the sp,
 * and a 4-byte method count. Then, for each method, it
 * reads a 4-byte method name index, a 4-byte param co-
 * unt for the method, and a 4-byte location of the me-
 * thod in the bytecode. Then, it constructs a Function
 * object with all this information and inserts it into
 * the blueprint's methods Table. */
static inline void handle_op_impl(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t blueprint_name_idx = READ_UINT32();
    uint32_t method_count = READ_UINT32();

    StructBlueprint *sb = table_get(vm->blueprints, code->sp.data[blueprint_name_idx]);
    if (!sb)
    {
        RUNTIME_ERROR("struct '%s' is not defined", code->sp.data[blueprint_name_idx]);
    }

    for (size_t i = 0; i < method_count; i++)
    {
        uint32_t method_name_idx = READ_UINT32();

        uint32_t paramcount = READ_UINT32();
        uint32_t location = READ_UINT32();

        Function method = {
            .location = location,
            .paramcount = paramcount,
            .name = code->sp.data[method_name_idx],
        };

        table_insert(sb->methods, code->sp.data[method_name_idx], ALLOC(method));
    }
}

static Upvalue *new_upvalue(Object *slot)
{
    Upvalue *upvalue = malloc(sizeof(Upvalue));
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

static Upvalue *capture_upvalue(VM *vm, Object *local)
{
    Upvalue *prev = NULL;
    Upvalue *current = vm->upvalues;

    while (current && current->location > local)
    {
        prev = current;
        current = current->next;
    }

    if (current && current->location == local)
        return current;

    Upvalue *created = new_upvalue(local);
    created->next = current;

    if (!prev)
        vm->upvalues = created;
    else
        prev->next = created;

    return created;
}

static void close_upvalues(VM *vm, Object *last)
{
    while (vm->upvalues && vm->upvalues->location >= last)
    {
        Upvalue *upvalue = vm->upvalues;

        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;

        vm->upvalues = upvalue->next;
    }
}

/* OP_CLOSURE reads a function name idx in the sp, a parameter
 * count, a function location in the bytecode, and the upvalue
 * count. Then, for each upvalue, it reads the upvalue index,
 * and captures it. Then, it constructs a Closure object with
 * all this information and pushes it on the stack. */
static inline void handle_op_closure(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t name_idx, paramcount, location, upvalue_count;
    Function f;
    Closure c;

    name_idx = READ_UINT32();
    paramcount = READ_UINT32();
    location = READ_UINT32();
    upvalue_count = READ_UINT32();

    f = (Function) {.name = code->sp.data[name_idx],
                    .paramcount = paramcount,
                    .location = location,
                    .upvalue_count = upvalue_count};

    c = (Closure) {
        .upvalues = malloc(sizeof(Upvalue *) * f.upvalue_count),
        .upvalue_count = f.upvalue_count,
        .refcount = 1,
        .func = ALLOC(f),
    };

    for (int i = 0; i < c.upvalue_count; i++)
    {
        uint32_t idx = READ_UINT32();
        c.upvalues[idx] = capture_upvalue(vm, &vm->stack[adjust_idx(vm, idx)]);
    }

    Object obj = CLOSURE_VAL(ALLOC(c));

    push(vm, obj);
}

/* OP_CALL reads a 4-byte number, argcount, and uses it to construct a
 * BytecodePtr object and push it on the frame pointer stack. The 'ar-
 * gcount' number is the number of argumentss the function was passed.
 *
 * The address BytecodePtr points to is the next instruction in seque-
 * nce that comes after the opcode and its 4-byte operand.
 *
 * The location is the starting position of the frame on the stack.
 *
 * The called function will be located on the top of the stack, so we
 * need to pop it.
 *
 * REFCOUNTING: Since the called function is a closure, and therefore
 * refcounted, we need to make sure to call objdecref on it. */
static inline void handle_op_call(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint8_t argcount = READ_UINT8();

    Object obj = pop(vm);
    objdecref(&obj);

    Closure *f = AS_CLOSURE(obj);

    BytecodePtr ip_obj = {.addr = *(ip), .location = vm->tos - argcount, .fn = f};
    vm->fp_stack[vm->fp_count++] = ip_obj;

    *ip = &code->code.data[f->func->location - 1];
}

/* OP_CALL_METHOD reads a 4-byte number, method_name_idx, which is the
 * index of the method name in the sp, and another 4-byte number, arg-
 * count, which it uses to construct a BytecodePtr object and push it
 * on the frame pointer stack.
 *
 * The address BytecodePtr points to is the next instruction in seque-
 * nce that comes after the opcode and its 4-byte operand.
 *
 * The location is the starting position of the frame on the stack. */
static inline void handle_op_call_method(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t method_name_idx = READ_UINT32();
    uint32_t argcount = READ_UINT32();

    Object object = peek(vm, argcount);

    /* Look up the method with that name on the blueprint. */
    Object *methodobj = table_get(AS_STRUCT(object)->properties, code->sp.data[method_name_idx]);
    if (!methodobj)
    {
        RUNTIME_ERROR("method '%s' is not defined on struct '%s'.", code->sp.data[method_name_idx],
                      AS_STRUCT(object)->name);
    }

    Closure *c = AS_CLOSURE(*methodobj);

    /* Push the instruction pointer on the frame ptr stack.
     * No need to take into account the jump sequence (+3). */
    BytecodePtr ip_obj = {.addr = *ip, .location = vm->tos - c->func->paramcount, .fn = c};
    vm->fp_stack[vm->fp_count++] = ip_obj;

    /* Direct jump to one byte before the method location. */
    *ip = &code->code.data[c->func->location - 1];
}

/* OP_RET pops a BytecodePtr off the frame pointer stack
 * and sets the instruction pointer to point to the add-
 * ress contained in the BytecodePtr. */
static inline void handle_op_ret(VM *vm, Bytecode *code, uint8_t **ip)
{
    BytecodePtr ptr = vm->fp_stack[--vm->fp_count];
    *ip = ptr.addr;
}

/* OP_POP pops an object off the stack.
 *
 * REFCOUNTING: Since the popped object might be refcounted,
 * its refcount must be decremented. */
static inline void handle_op_pop(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object obj = pop(vm);
    objdecref(&obj);
}

/* OP_DEREF pops an object off the stack, dereferences it
 * and pushes it back on the stack.
 *
 * REFCOUNTING: Since the object will now be present in one
 * more another location, its refcount must be incremented. */
static inline void handle_op_deref(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object ptrobj = pop(vm);

    push(vm, *AS_PTR(ptrobj));
    objincref(&*AS_PTR(ptrobj));
}

/* OP_STRCAT pops two objects off the stack, checks whether they
 * are both strings and if so, concatenates them, and pushes the
 * resulting string back on the stack.
 *
 * REFCOUNTING:
 *
 * Since Strings are refcounted objects, their refcounts must be
 * decremented.
 *
 * The resulting string is initalized with the refcount of 1. */
static inline void handle_op_strcat(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object b = pop(vm);
    Object a = pop(vm);

    if (IS_STRING(a) && IS_STRING(b))
    {
        char *result = concatenate_strings(AS_STRING(a)->value, AS_STRING(b)->value);

        String s = {.refcount = 1, .value = result};

        push(vm, STRING_VAL(ALLOC(s)));

        objdecref(&b);
        objdecref(&a);
    }
    else
    {
        RUNTIME_ERROR("'++' operator used on objects of unsupported types: %s and %s",
                      get_object_type(&a), get_object_type(&b));
    }
}

/* OP_ARRAY reads a 4-byte count of the array elements, pops that many ele-
 * ments off the stack, inserts them into a dynarray, creates an Array obj-
 * ect, and pushes it on the stack.
 *
 * REFCOUNTING: Since Arrays are refcounted, the new object has refcount=1. */
static inline void handle_op_array(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t count = READ_UINT32();

    DynArray_Object elements = {0};
    for (size_t i = 0; i < count; i++)
        dynarray_insert(&elements, pop(vm));

    Array array = {.refcount = 1, .elements = elements};

    push(vm, ARRAY_VAL(ALLOC(array)));
}

/* OP_ARRAYSET pops three objects off the stack: the index, the array object,
 * and the value. Then, it reassigns the element within the array at that idx
 * to 'value'.
 *
 * REFCOUNTING: We need to make sure to decrement the refcount for the popped
 * array, since arrays are refcounted objects. */
static inline void handle_op_arrayset(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object value = pop(vm);
    Object index = pop(vm);
    Object subscriptee = pop(vm);
    Array *array = AS_ARRAY(subscriptee);
    array->elements.data[(int) AS_NUM(index)] = value;
    objdecref(&subscriptee);
}

/* OP_SUBSCRIPT pops two objects off the stack, index, and the subscriptee
 * which is expected to be an array. Then, it accesses the object at index
 * 'index' within the array object, and pushes it on the stack.
 *
 * REFCOUNTING: We need to make sure to decrement the refcount for the po-
 * pped array, and increment the refcount for the object we are pushing on
 * the stack. */
static inline void handle_op_subscript(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object index = pop(vm);
    Object object = pop(vm);
    Object value = AS_ARRAY(object)->elements.data[(int) AS_NUM(index)];
    push(vm, value);
    objincref(&value);
    objdecref(&object);
}

/* OP_GET_UPVALUE reads a 4-byte index of the upvalue and pushes it on the
 * stack.
 *
 * REFCOUNTING: Since the pushed value is now present in one mor eplace, we
 * need to make sure to increment the refcount. */
static inline void handle_op_get_upvalue(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t idx = READ_UINT32();
    Object *obj = vm->fp_stack[vm->fp_count - 1].fn->upvalues[idx]->location;
    objincref(obj);
    push(vm, *obj);
}

/* OP_GET_UPVALUE_PTR reads a 4-byte index of the upvalue and pushes it
 * on the stack. It's exactly like OP_GET_UPVALUE, differing in that it
 * pushes /the address/ of the object instead of the object itself. */
static inline void handle_op_get_upvalue_ptr(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t idx = READ_UINT32();
    Object *obj = vm->fp_stack[vm->fp_count - 1].fn->upvalues[idx]->location;
    push(vm, PTR_VAL(obj));
}

/* OP_SET_UPVALUE reads a 4-byte index of the upvalue and sets it to the
 * previously popped object.
 *
 * REFCOUNTING: Since the target value will now be gone from that place,
 * we need to make sure to decrement its refcount. */
static inline void handle_op_set_upvalue(VM *vm, Bytecode *code, uint8_t **ip)
{
    uint32_t idx = READ_UINT32();
    Object obj = pop(vm);
    objdecref(vm->fp_stack[vm->fp_count - 1].fn->upvalues[idx]->location);
    *vm->fp_stack[vm->fp_count - 1].fn->upvalues[idx]->location = obj;
}

/* OP_CLOSE_UPVALUE is a part of the stack cleanup procedure and runs upon
 * returning from the function. The push/pop dance is to preserve the ret-
 * urn value. */
static inline void handle_op_close_upvalue(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object result = pop(vm);
    close_upvalues(vm, &vm->stack[vm->tos - 1]);
    pop(vm);
    push(vm, result);
}

/* OP_MKGEN pops the closure off of the stack, makes a generator object
 * out of it, and pushes it on the stack.
 *
 * REFCOUNTING: Since the popped object is a closure and therefore ref-
 * counted, it is necessary to decrement the refcount. */
static inline void handle_op_mkgen(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object closure = pop(vm);
    objdecref(&closure);
    Generator gen = {
        .refcount = 1, .fn = AS_CLOSURE(closure), .tos = 0, .fp_count = 0, .state = STATE_NEW};
    gen.ip = &code->code.data[AS_CLOSURE(closure)->func->location - 1];
    push(vm, GENERATOR_VAL(ALLOC(gen)));
}

/* OP_YIELD takes a snapshot of the running generator's stacks (main stack
 * and frame pointer stack), and then restores the caller's context onto the
 * main VM stack.  Finally, it continues the execution from where the caller
 * was suspended. */
static inline void handle_op_yield(VM *vm, Bytecode *code, uint8_t **ip)
{
    Generator *gen = vm->gen_stack[--vm->gen_count];
    FrameSnapshot *fs = vm->fs_stack[--vm->fs_count];

    Object yielded = pop(vm);

    memcpy(gen->stack, vm->stack, sizeof(Object) * vm->tos);
    gen->tos = vm->tos;
    memcpy(gen->fp_stack, vm->fp_stack, sizeof(BytecodePtr) * vm->fp_count);
    gen->fp_count = vm->fp_count;

    memcpy(vm->stack, fs->stack, sizeof(Object) * fs->tos);
    vm->tos = fs->tos;
    memcpy(vm->fp_stack, fs->fp_stack, sizeof(BytecodePtr) * fs->fp_count);
    vm->fp_count = fs->fp_count;

    uint8_t *tmp = *ip;
    *ip = gen->ip;
    gen->ip = tmp;

    push(vm, yielded);

    gen->state = STATE_SUSPENDED;

    free(fs);
}

/* OP_RESUME pops a generator object off of the main VM stack, and if it's
 * a fresh, new generator, it will push the frame pointer onto the fp stack.
 * Then it takes a snapshot of the main VM stack, and restores the resumed
 * generator's context. */
static inline void handle_op_resume(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object obj = pop(vm);
    objdecref(&obj);

    Generator *gen = AS_GENERATOR(obj);

    if (gen->state == STATE_NEW)
    {
        BytecodePtr ptr = {
            .addr = vm->fp_stack[vm->fp_count - 1].addr, .fn = gen->fn, .location = vm->tos - 1};
        vm->fp_stack[vm->fp_count++] = ptr;
    }

    FrameSnapshot fs = {.tos = vm->tos, .ip = *ip, .fp_count = vm->fp_count};
    memcpy(fs.stack, vm->stack, sizeof(Object) * vm->tos);
    memcpy(fs.fp_stack, vm->fp_stack, sizeof(BytecodePtr) * vm->fp_count);
    vm->fs_stack[vm->fs_count++] = ALLOC(fs);

    memcpy(vm->stack, gen->stack, sizeof(Object) * gen->tos);
    vm->tos = gen->tos;
    memcpy(vm->fp_stack, gen->fp_stack, sizeof(BytecodePtr) * gen->fp_count);
    vm->fp_count = gen->fp_count;

    uint8_t *tmp_ip = *ip;
    *ip = gen->ip;
    gen->ip = tmp_ip;

    gen->state = STATE_ACTIVE;

    vm->gen_stack[vm->gen_count++] = gen;
}

static inline void handle_op_len(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object obj = pop(vm);
    objdecref(&obj);

    if (IS_STRING(obj))
        push(vm, NUM_VAL(strlen(AS_STRING(obj)->value)));
    else if (IS_ARRAY(obj))
        push(vm, NUM_VAL(AS_ARRAY(obj)->elements.count));
    else
        RUNTIME_ERROR("cannot get len() of type '%s'.", get_object_type(&obj));
}

static inline void handle_op_hasattr(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object attr = pop(vm);
    Object obj = pop(vm);

    if (!IS_STRUCT(obj))
        RUNTIME_ERROR("can only hasattr() structs");

    Object *found = table_get(AS_STRUCT(obj)->properties, AS_STRING(attr)->value);

    push(vm, !found ? BOOL_VAL(false) : BOOL_VAL(true));

    objdecref(&obj);
    objdecref(&attr);
}

static inline void handle_op_assert(VM *vm, Bytecode *code, uint8_t **ip)
{
    Object assertion = pop(vm);

    if (!AS_BOOL(assertion))
        RUNTIME_ERROR("assertion failed");
}

#ifdef venom_debug_vm
static inline const char *print_current_instruction(uint8_t opcode)
{
    switch (opcode)
    {
        case OP_PRINT:
            return "OP_PRINT";
        case OP_ADD:
            return "OP_ADD";
        case OP_SUB:
            return "OP_SUB";
        case OP_MUL:
            return "OP_MUL";
        case OP_DIV:
            return "OP_DIV";
        case OP_MOD:
            return "OP_MOD";
        case OP_EQ:
            return "OP_EQ";
        case OP_GT:
            return "OP_GT";
        case OP_LT:
            return "OP_LT";
        case OP_NOT:
            return "OP_NOT";
        case OP_NEG:
            return "OP_NEG";
        case OP_TRUE:
            return "OP_TRUE";
        case OP_NULL:
            return "OP_NULL";
        case OP_CONST:
            return "OP_CONST";
        case OP_STR:
            return "OP_STR";
        case OP_JZ:
            return "OP_JZ";
        case OP_JMP:
            return "OP_JMP";
        case OP_BITAND:
            return "OP_BITAND";
        case OP_BITOR:
            return "OP_BITOR";
        case OP_BITXOR:
            return "OP_BITXOR";
        case OP_BITNOT:
            return "OP_BITNOT";
        case OP_BITSHL:
            return "OP_BITSHL";
        case OP_BITSHR:
            return "OP_BITSHR";
        case OP_SET_GLOBAL:
            return "OP_SET_GLOBAL";
        case OP_GET_GLOBAL:
            return "OP_GET_GLOBAL";
        case OP_GET_GLOBAL_PTR:
            return "OP_GET_GLOBAL_PTR";
        case OP_DEEPSET:
            return "OP_DEEPSET";
        case OP_DEEPGET:
            return "OP_DEEPGET";
        case OP_DEEPGET_PTR:
            return "OP_DEEPGET_PTR";
        case OP_SETATTR:
            return "OP_SETATTR";
        case OP_GETATTR:
            return "OP_GETATTR";
        case OP_GETATTR_PTR:
            return "OP_GETATTR_PTR";
        case OP_STRUCT:
            return "OP_STRUCT";
        case OP_STRUCT_BLUEPRINT:
            return "OP_STRUCT_BLUEPRINT";
        case OP_CLOSURE:
            return "OP_CLOSURE";
        case OP_CALL:
            return "OP_CALL";
        case OP_CALL_METHOD:
            return "OP_CALL_METHOD";
        case OP_RET:
            return "OP_RET";
        case OP_POP:
            return "OP_POP";
        case OP_DEREF:
            return "OP_DEREF";
        case OP_DEREFSET:
            return "OP_DEREFSET";
        case OP_STRCAT:
            return "OP_STRCAT";
        case OP_ARRAY:
            return "OP_ARRAY";
        case OP_ARRAYSET:
            return "OP_ARRAYSET";
        case OP_SUBSCRIPT:
            return "OP_SUBSCRIPT";
        case OP_GET_UPVALUE:
            return "OP_GET_UPVALUE";
        case OP_GET_UPVALUE_PTR:
            return "OP_GET_UPVALUE_PTR";
        case OP_SET_UPVALUE:
            return "OP_SET_UPVALUE";
        case OP_CLOSE_UPVALUE:
            return "OP_CLOSE_UPVALUE";
        case OP_IMPL:
            return "OP_IMPL";
        case OP_MKGEN:
            return "OP_MKGEN";
        case OP_YIELD:
            return "OP_YIELD";
        case OP_RESUME:
            return "OP_RESUME";
        case OP_LEN:
            return "OP_LEN";
        case OP_HASATTR:
            return "OP_HASATTR";
        case OP_ASSERT:
            return "OP_ASSERT";
        case OP_HLT:
            return "OP_HLT";
        default:
            assert(0);
    }
}
#endif

void run(VM *vm, Bytecode *code)
{
    static void *dispatch_table[] = {
        &&op_print,
        &&op_add,
        &&op_sub,
        &&op_mul,
        &&op_div,
        &&op_mod,
        &&op_eq,
        &&op_gt,
        &&op_lt,
        &&op_not,
        &&op_neg,
        &&op_true,
        &&op_null,
        &&op_const,
        &&op_str,
        &&op_jmp,
        &&op_jz,
        &&op_bitand,
        &&op_bitor,
        &&op_bitxor,
        &&op_bitnot,
        &&op_bitshl,
        &&op_bitshr,
        &&op_set_global,
        &&op_get_global,
        &&op_get_global_ptr,
        &&op_deepset,
        &&op_deepget,
        &&op_deepget_ptr,
        &&op_setattr,
        &&op_getattr,
        &&op_getattr_ptr,
        &&op_struct,
        &&op_struct_blueprint,
        &&op_closure,
        &&op_call,
        &&op_call_method,
        &&op_ret,
        &&op_pop,
        &&op_deref,
        &&op_derefset,
        &&op_strcat,
        &&op_array,
        &&op_arrayset,
        &&op_subscript,
        &&op_get_upvalue,
        &&op_get_upvalue_ptr,
        &&op_set_upvalue,
        &&op_close_upvalue,
        &&op_impl,
        &&op_mkgen,
        &&op_yield,
        &&op_resume,
        &&op_len,
        &&op_hasattr,
        &&op_assert,
        &&op_hlt,
    };

#ifndef venom_debug_vm
#define DISPATCH() goto *dispatch_table[*++ip]
#else
#define DISPATCH()                                                             \
    do                                                                         \
    {                                                                          \
        PRINT_STACK();                                                         \
        PRINT_FPSTACK();                                                       \
        printf("%ld: ", ip - code->code.data + 1);                             \
        printf("current instruction: %s\n", print_current_instruction(*++ip)); \
        goto *dispatch_table[*ip];                                             \
    } while (0)
#endif

    uint8_t *ip = code->code.data;

    goto *dispatch_table[*ip];

    while (1)
    {
    op_print:
        handle_op_print(vm, code, &ip);
        DISPATCH();
    op_add:
        handle_op_add(vm, code, &ip);
        DISPATCH();
    op_sub:
        handle_op_sub(vm, code, &ip);
        DISPATCH();
    op_mul:
        handle_op_mul(vm, code, &ip);
        DISPATCH();
    op_div:
        handle_op_div(vm, code, &ip);
        DISPATCH();
    op_mod:
        handle_op_mod(vm, code, &ip);
        DISPATCH();
    op_eq:
        handle_op_eq(vm, code, &ip);
        DISPATCH();
    op_gt:
        handle_op_gt(vm, code, &ip);
        DISPATCH();
    op_lt:
        handle_op_lt(vm, code, &ip);
        DISPATCH();
    op_not:
        handle_op_not(vm, code, &ip);
        DISPATCH();
    op_neg:
        handle_op_neg(vm, code, &ip);
        DISPATCH();
    op_true:
        handle_op_true(vm, code, &ip);
        DISPATCH();
    op_null:
        handle_op_null(vm, code, &ip);
        DISPATCH();
    op_const:
        handle_op_const(vm, code, &ip);
        DISPATCH();
    op_str:
        handle_op_str(vm, code, &ip);
        DISPATCH();
    op_jmp:
        handle_op_jmp(vm, code, &ip);
        DISPATCH();
    op_jz:
        handle_op_jz(vm, code, &ip);
        DISPATCH();
    op_bitand:
        handle_op_bitand(vm, code, &ip);
        DISPATCH();
    op_bitor:
        handle_op_bitor(vm, code, &ip);
        DISPATCH();
    op_bitxor:
        handle_op_bitxor(vm, code, &ip);
        DISPATCH();
    op_bitnot:
        handle_op_bitnot(vm, code, &ip);
        DISPATCH();
    op_bitshl:
        handle_op_bitshl(vm, code, &ip);
        DISPATCH();
    op_bitshr:
        handle_op_bitshr(vm, code, &ip);
        DISPATCH();
    op_set_global:
        handle_op_set_global(vm, code, &ip);
        DISPATCH();
    op_get_global:
        handle_op_get_global(vm, code, &ip);
        DISPATCH();
    op_get_global_ptr:
        handle_op_get_global_ptr(vm, code, &ip);
        DISPATCH();
    op_deepset:
        handle_op_deepset(vm, code, &ip);
        DISPATCH();
    op_deepget:
        handle_op_deepget(vm, code, &ip);
        DISPATCH();
    op_deepget_ptr:
        handle_op_deepget_ptr(vm, code, &ip);
        DISPATCH();
    op_setattr:
        handle_op_setattr(vm, code, &ip);
        DISPATCH();
    op_getattr:
        handle_op_getattr(vm, code, &ip);
        DISPATCH();
    op_getattr_ptr:
        handle_op_getattr_ptr(vm, code, &ip);
        DISPATCH();
    op_struct:
        handle_op_struct(vm, code, &ip);
        DISPATCH();
    op_struct_blueprint:
        handle_op_struct_blueprint(vm, code, &ip);
        DISPATCH();
    op_closure:
        handle_op_closure(vm, code, &ip);
        DISPATCH();
    op_call:
        handle_op_call(vm, code, &ip);
        DISPATCH();
    op_call_method:
        handle_op_call_method(vm, code, &ip);
        DISPATCH();
    op_ret:
        handle_op_ret(vm, code, &ip);
        DISPATCH();
    op_pop:
        handle_op_pop(vm, code, &ip);
        DISPATCH();
    op_deref:
        handle_op_deref(vm, code, &ip);
        DISPATCH();
    op_derefset:
        handle_op_derefset(vm, code, &ip);
        DISPATCH();
    op_strcat:
        handle_op_strcat(vm, code, &ip);
        DISPATCH();
    op_array:
        handle_op_array(vm, code, &ip);
        DISPATCH();
    op_arrayset:
        handle_op_arrayset(vm, code, &ip);
        DISPATCH();
    op_subscript:
        handle_op_subscript(vm, code, &ip);
        DISPATCH();
    op_get_upvalue:
        handle_op_get_upvalue(vm, code, &ip);
        DISPATCH();
    op_get_upvalue_ptr:
        handle_op_get_upvalue_ptr(vm, code, &ip);
        DISPATCH();
    op_set_upvalue:
        handle_op_set_upvalue(vm, code, &ip);
        DISPATCH();
    op_close_upvalue:
        handle_op_close_upvalue(vm, code, &ip);
        DISPATCH();
    op_impl:
        handle_op_impl(vm, code, &ip);
        DISPATCH();
    op_mkgen:
        handle_op_mkgen(vm, code, &ip);
        DISPATCH();
    op_yield:
        handle_op_yield(vm, code, &ip);
        DISPATCH();
    op_resume:
        handle_op_resume(vm, code, &ip);
        DISPATCH();
    op_len:
        handle_op_len(vm, code, &ip);
        DISPATCH();
    op_hasattr:
        handle_op_hasattr(vm, code, &ip);
        DISPATCH();
    op_assert:
        handle_op_assert(vm, code, &ip);
        DISPATCH();
    op_hlt:
        assert(vm->tos == 0);
        return;
    }
}