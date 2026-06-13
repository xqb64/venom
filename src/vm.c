#include "vm.h"

#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "compiler.h"

#ifdef venom_debug_vm
#include "disassembler.h"
#endif

#include "dynarray.h"
#include "math.h"
#include "object.h"
#include "table.h"
#include "util.h"

void init_vm(VM *vm)
{
  memset(vm, 0, sizeof(VM));
  vm->fp_count = 1;
  vm->fp_stack[0] = (BytecodePtr){0};
  vm->blueprints = calloc(1, sizeof(Table_StructBlueprint));
}

void free_vm(VM *vm)
{
  for (size_t i = 0; i < vm->task_count; i++) {
    Object task_obj = TASK_VAL(vm->tasks[i]);
    objdecref(&task_obj);
  }
  if (vm->scheduler_frame) {
    free(vm->scheduler_frame);
  }
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

static void dealloc_stack(VM *vm)
{
  for (int i = (int) vm->tos - 1; i >= 0; i--) {
    objdecref(&vm->stack[i]);
  }
}

static inline uint64_t clamp(double d)
{
  if (d < 0.0) {
    return 0;
  } else if (d > (double) UINT64_MAX) {
    return UINT64_MAX;
  } else {
    return (uint64_t) d;
  }
}

#define UNLIKELY(exp) (!!(__builtin_expect((exp), 0)))

#define BINARY_OP_FAST(op)                          \
  do {                                              \
    Object *lhs = &vm->stack[vm->tos - 2];          \
    Object *rhs = &vm->stack[vm->tos - 1];          \
                                                    \
    if (UNLIKELY(!IS_NUM(*lhs) || !IS_NUM(*rhs))) { \
      objdecref(rhs);                               \
      objdecref(lhs);                               \
                                                    \
      RUNTIME_ERROR("operands must be numbers");    \
    }                                               \
                                                    \
    *lhs = NUM_VAL(AS_NUM(*lhs) op AS_NUM(*rhs));   \
                                                    \
    vm->tos--;                                      \
  } while (0)

#define BINARY_OP(op, wrapper)                                          \
  do {                                                                  \
    Object b = pop(vm);                                                 \
    Object a = pop(vm);                                                 \
                                                                        \
    if (UNLIKELY(!IS_NUM(a) || !IS_NUM(b))) {                           \
      objdecref(&b);                                                    \
      objdecref(&a);                                                    \
                                                                        \
      RUNTIME_ERROR("cannot '" #op "' objects of types: '%s' and '%s'", \
                    get_object_type(&a), get_object_type(&b));          \
    }                                                                   \
                                                                        \
    /* No need to decref here as they're nums.  */                      \
                                                                        \
    Object obj = wrapper(AS_NUM(a) op AS_NUM(b));                       \
                                                                        \
    push(vm, obj);                                                      \
  } while (0)

#define BITWISE_OP_FAST(op)                                             \
  do {                                                                  \
    Object *lhs = &vm->stack[vm->tos - 2];                              \
    Object *rhs = &vm->stack[vm->tos - 1];                              \
                                                                        \
    if (UNLIKELY(!IS_NUM(*lhs) || !IS_NUM(*rhs))) {                     \
      objdecref(lhs);                                                   \
      objdecref(rhs);                                                   \
                                                                        \
      RUNTIME_ERROR("cannot '" #op "' objects of types: '%s' and '%s'", \
                    get_object_type(lhs), get_object_type(rhs));        \
    }                                                                   \
                                                                        \
    uint64_t clamped_a = clamp(AS_NUM(*lhs));                           \
    uint64_t clamped_b = clamp(AS_NUM(*rhs));                           \
                                                                        \
    uint64_t result = clamped_a op clamped_b;                           \
                                                                        \
    *lhs = NUM_VAL((double) result);                                    \
                                                                        \
    vm->tos--;                                                          \
  } while (0)

#define BITWISE_OP(op)                                                  \
  do {                                                                  \
    Object b = pop(vm);                                                 \
    Object a = pop(vm);                                                 \
                                                                        \
    if (!IS_NUM(a) || !IS_NUM(b)) {                                     \
      objdecref(&b);                                                    \
      objdecref(&a);                                                    \
                                                                        \
      RUNTIME_ERROR("cannot '" #op "' objects of types: '%s' and '%s'", \
                    get_object_type(&a), get_object_type(&b));          \
    }                                                                   \
                                                                        \
    uint64_t clamped_a = clamp(AS_NUM(a));                              \
    uint64_t clamped_b = clamp(AS_NUM(b));                              \
                                                                        \
    uint64_t result = clamped_a op clamped_b;                           \
                                                                        \
    Object obj = NUM_VAL((double) result);                              \
                                                                        \
    push(vm, obj);                                                      \
  } while (0)

#define READ_UINT8() (*++(*ip))

#define READ_INT16()                               \
  /* ip points to one of the jump instructions and \
   * there is a 2-byte operand (offset) that comes \
   * after the opcode. The instruction pointer ne- \
   * eds to be incremented to point to the last of \
   * the two operands, and a 16-bit offset constr- \
   * ucted from the two bytes. Then the ip will be \
   * incremented by the mainloop again to point to \
   * the next opcode that comes after the jump. */ \
  (*ip += 2, (int16_t) (((*ip)[-1] << 8) | (*ip)[0]))

#define READ_UINT32()                                            \
  (*ip += 4, (uint32_t) (((*ip)[-3] << 24) | ((*ip)[-2] << 16) | \
                         ((*ip)[-1] << 8) | (*ip)[0]))

#define READ_DOUBLE()                                                       \
  (*ip += 8, (((uint64_t) (*ip)[-7] << 56) | ((uint64_t) (*ip)[-6] << 48) | \
              ((uint64_t) (*ip)[-5] << 40) | ((uint64_t) (*ip)[-4] << 32) | \
              ((uint64_t) (*ip)[-3] << 24) | ((uint64_t) (*ip)[-2] << 16) | \
              ((uint64_t) (*ip)[-1] << 8) | (uint64_t) (*ip)[0]))

#define PRINT_STACK()                      \
  do {                                     \
    printf("stack: [");                    \
                                           \
    for (size_t i = 0; i < vm->tos; i++) { \
      print_object(&vm->stack[i]);         \
      if (i < vm->tos - 1) {               \
        printf(", ");                      \
      }                                    \
    }                                      \
                                           \
    printf("]\n");                         \
  } while (0)

#define PRINT_FPSTACK()                                        \
  do {                                                         \
    printf("fp stack: [");                                     \
                                                               \
    for (size_t i = 1; i < vm->fp_count; i++) {                \
      printf("<%s (loc: %d)>", vm->fp_stack[i].fn->func->name, \
             vm->fp_stack[i].location);                        \
      if (i < vm->fp_count - 1) {                              \
        printf(", ");                                          \
      }                                                        \
    }                                                          \
                                                               \
    printf("]\n");                                             \
  } while (0)

#define RUNTIME_ERROR(...)                    \
  do {                                        \
    alloc_err_str(&vm->err_msg, __VA_ARGS__); \
    dealloc_stack(vm);                        \
    longjmp(vm->trap, -1);                    \
  } while (0)

static inline bool check_equality(Object *left, Object *right)
{
#ifdef NAN_BOXING
  if (IS_NUM(*left) && IS_NUM(*right)) {
    return AS_NUM(*left) == AS_NUM(*right);
  } else if (IS_STRING(*left) && IS_STRING(*right)) {
    return strcmp(AS_STRING(*left)->value, AS_STRING(*right)->value) == 0;
  }
  return *left == *right;
#else

  /* Return false if the objects are of different type. */
  if (left->type != right->type) {
    return false;
  }

  switch (left->type) {
    case OBJ_NUMBER: {
      return AS_NUM(*left) == AS_NUM(*right);
    }
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
    default:
      assert(0);
  }
#endif
}

static inline uint32_t adjust_idx(VM *vm, uint8_t idx)
{
  /* 'idx' is adjusted to be relative to the current fra-
   * me pointer, */
  return vm->fp_base + idx;
}

static inline void push_frame(VM *vm, BytecodePtr frame)
{
  vm->fp_stack[vm->fp_count++] = frame;
  vm->fp_base = frame.location;
}

static inline BytecodePtr pop_frame(VM *vm)
{
  BytecodePtr frame = vm->fp_stack[--vm->fp_count];
  vm->fp_base = vm->fp_stack[vm->fp_count - 1].location;
  return frame;
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
static inline void handle_op_print(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
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
 * pushes the result back on the stack. */
static inline void handle_op_add(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  BINARY_OP_FAST(+);
}

/* OP_SUB pops two objects off the stack, subs them, and
 * pushes the result back on the stack. */
static inline void handle_op_sub(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  BINARY_OP_FAST(-);
}

/* OP_MUL pops two objects off the stack, muls them, and
 * pushes the result back on the stack. */
static inline void handle_op_mul(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  BINARY_OP_FAST(*);
}

/* OP_DIV pops two objects off the stack, divs them, and
 * pushes the result back on the stack. */
static inline void handle_op_div(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  BINARY_OP_FAST(/);
}

/* OP_MOD pops two objects off the stack, mods them, and
 * pushes the result back on the stack. */
static inline void handle_op_mod(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  Object b = pop(vm);
  Object a = pop(vm);

  if (!IS_NUM(a) || !IS_NUM(b)) {
    objdecref(&b);
    objdecref(&a);

    RUNTIME_ERROR("cannot '%%' objects of types: '%s' and '%s'",
                  get_object_type(&a), get_object_type(&b));
  }

  Object obj = NUM_VAL(fmod(AS_NUM(a), AS_NUM(b)));

  push(vm, obj);
}

/* OP_BITAND pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise AND operati-
 * on on them, and pushes the result back on the stack. */
static inline void handle_op_bitand(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  BITWISE_OP_FAST(&);
}

/* OP_BITOR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise OR operati-
 * on on them, and pushes the result back on the stack. */
static inline void handle_op_bitor(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
{
  BITWISE_OP_FAST(|);
}

/* OP_BITXOR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise XOR operati-
 * on on them, and pushes the result back on the stack. */
static inline void handle_op_bitxor(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  BITWISE_OP_FAST(^);
}

/* OP_BITNOT pops an object off the stack, clamps it to
 * to [0, UINT64_MAX], performs the bitwise NOT operat-
 * ion on it, and pushes the result back on the stack. */
static inline void handle_op_bitnot(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  Object obj = pop(vm);

  if (!IS_NUM(obj)) {
    objdecref(&obj);
    RUNTIME_ERROR("cannot '~' objects of type: '%s'", get_object_type(&obj));
  }

  uint64_t clamped = clamp(AS_NUM(obj));
  uint64_t inverted = ~clamped;

  push(vm, NUM_VAL(inverted));
}

/* OP_BITSHL pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise SHL operati-
 * on on them, and pushes the result back on the stack. */
static inline void handle_op_bitshl(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  BITWISE_OP_FAST(<<);
}

/* OP_BITSHR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise SHR operati-
 * on on them, and pushes the result back on the stack. */
static inline void handle_op_bitshr(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  BITWISE_OP_FAST(>>);
}

/* OP_EQ pops two objects off the stack, clamps them to
 * [0, UINT64_MAX], performs the equality check on them
 * and pushes the result on the stack.
 *
 * REFCOUNTING: Since the two objects might be refcoun-
 * ted, the reference count for both must be decrement-
 * ed. */
static inline void handle_op_eq(VM *restrict vm, const Bytecode *restrict code,
                                uint8_t *restrict *ip)
{
  Object b = pop(vm);
  Object a = pop(vm);

  bool eq = check_equality(&a, &b);

  objdecref(&a);
  objdecref(&b);

  push(vm, BOOL_VAL(eq));
}

/* OP_GT pops two objects off the stack, compares them us-
 * ing the GT operation, and pushes the result back on the
 * stack. */
static inline void handle_op_gt(VM *restrict vm, const Bytecode *restrict code,
                                uint8_t *restrict *ip)
{
  BINARY_OP(>, BOOL_VAL);
}

/* OP_LT pops two objects off the stack, compares them us-
 * ing the LT operation, and pushes the result back on the
 * stack. */
static inline void handle_op_lt(VM *restrict vm, const Bytecode *restrict code,
                                uint8_t *restrict *ip)
{
  BINARY_OP(<, BOOL_VAL);
}

/* OP_NOT pops an object off the stack, performs the
 * logical NOT operation on it by inverting its bool
 * value, and pushes the result back on the stack. */
static inline void handle_op_not(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  Object obj = pop(vm);

  if (!IS_BOOL(obj)) {
    objdecref(&obj);
    RUNTIME_ERROR("cannot '!' objects of type: '%s'", get_object_type(&obj));
  }

  push(vm, BOOL_VAL(AS_BOOL(obj) ^ 1));
}

/* OP_NEG pops an object off the stack, performs the
 * logical NEG operation on it by negating its value,
 * and pushes the result back on the stack. */
static inline void handle_op_neg(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  Object original = pop(vm);

  if (!IS_NUM(original)) {
    objdecref(&original);
    RUNTIME_ERROR("cannot '-' objects of type: '%s'",
                  get_object_type(&original));
  }

  Object negated = NUM_VAL(-AS_NUM(original));
  push(vm, negated);
}

/* OP_TRUE pushes a bool object ('true') on the stack. */
static inline void handle_op_true(VM *restrict vm, const Bytecode *restrict code,
                                  uint8_t *restrict *ip)
{
  push(vm, BOOL_VAL(true));
}

/* OP_NULL pushes a null object on the stack. */
static inline void handle_op_null(VM *restrict vm, const Bytecode *restrict code,
                                  uint8_t *restrict *ip)
{
  push(vm, NULL_VAL);
}

/* OP_CONST reads a 4-byte index of the constant in the
 * chunk's cp, constructs an object with that value and
 * pushes it on the stack. */
static inline void handle_op_const(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
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
static inline void handle_op_str(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  uint8_t idx = READ_UINT8();

  String s = {.refcount = 1, .value = own_string(code->sp.data[idx])};
  push(vm, STRING_VAL(ALLOC(s)));
}

/* OP_JZ reads a signed 2-byte offset (that could be ne-
 * gative), pops an object off the stack, and increments
 * the instruction pointer by the offset, if and only if
 * the popped object was 'false'. */
static inline void handle_op_jz(VM *restrict vm, const Bytecode *restrict code,
                                uint8_t *restrict *ip)
{
  int16_t offset = READ_INT16();

  Object obj = pop(vm);
  *ip += offset * !AS_BOOL(obj);
}

/* OP_JMP reads a signed 2-byte offset (that could be ne-
 * gative), and increments the instruction pointer by the
 * offset. Unlike OP_JZ, which is a conditional jump, the
 * OP_JMP instruction takes the jump unconditionally. */
static inline void handle_op_jmp(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
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
static inline void handle_op_set_global(VM *restrict vm, const Bytecode *restrict code,
                                        uint8_t *restrict *ip)
{
  uint8_t name_idx = READ_UINT8();

  Object obj = pop(vm);

  Object *target = table_get(&vm->globals, code->sp.data[name_idx]);
  if (target) {
    objdecref(target);
  }

  table_insert(&vm->globals, code->sp.data[name_idx], obj);
}

/* OP_GET_GLOBAL reads a 4-byte index of the variable name
 * in the chunk's sp, looks up an object with that name in
 * the vm's globals table, and pushes it on the stack.
 *
 * REFCOUNTING: Since the object will be present in yet an-
 * other location, the refcount must be incremented. */
static inline void handle_op_get_global(VM *restrict vm, const Bytecode *restrict code,
                                        uint8_t *restrict *ip)
{
  uint8_t name_idx = READ_UINT8();

  Object *obj = table_get_unchecked(&vm->globals, code->sp.data[name_idx]);
  push(vm, *obj);

  objincref(obj);
}

/* OP_GET_GLOBAL_PTR reads a 4-byte index of the variable
 * name in the chunk's sp, looks up the object under that
 * name in in the vm's globals table, and pushes its add-
 * ress on the stack. */
static inline void handle_op_get_global_ptr(VM *vm,
                                            const Bytecode *restrict code,
                                            uint8_t *restrict *ip)
{
  uint8_t name_idx = READ_UINT8();

  Object *object_ptr =
      table_get_unchecked(&vm->globals, code->sp.data[name_idx]);

  push(vm, PTR_VAL(object_ptr));
}

/* OP_DEEPSET reads a 4-byte index (1-based) of the obj-
 * ect being modified, which is adjusted and used to set
 * the object in that position to the popped object.
 *
 * REFCOUNTING: Since the object being set will be over-
 * written, its reference count must be decremented bef-
 * ore putting the popped object into that position. */
static inline void handle_op_deepset(VM *restrict vm, const Bytecode *restrict code,
                                     uint8_t *restrict *ip)
{
  uint8_t idx = READ_UINT8();
  size_t adjusted_idx = adjust_idx(vm, idx);

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
static inline void handle_op_derefset(VM *restrict vm, const Bytecode *restrict code,
                                      uint8_t *restrict *ip)
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
static inline void handle_op_deepget(VM *restrict vm, const Bytecode *restrict code,
                                     uint8_t *restrict *ip)
{
  uint8_t idx = READ_UINT8();
  size_t adjusted_idx = adjust_idx(vm, idx);

  Object obj = vm->stack[adjusted_idx];
  push(vm, obj);

  objincref(&obj);
}

/* OP_DEEPGET_PTR reads a 4-byte index (1-based) of the
 * object being accessed, which is adjusted and used to
 * access the object in that position and push its add-
 * ress on the stack. */
static inline void handle_op_deepget_ptr(VM *restrict vm, const Bytecode *restrict code,
                                         uint8_t *restrict *ip)
{
  uint8_t idx = READ_UINT8();
  size_t adjusted_idx = adjust_idx(vm, idx);

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
static inline void handle_op_setattr(VM *restrict vm, const Bytecode *restrict code,
                                     uint8_t *restrict *ip)
{
  uint8_t property_name_idx = READ_UINT8();

  Object value = pop(vm);
  Object obj = pop(vm);

  if (!IS_STRUCT(obj)) {
    objdecref(&value);
    objdecref(&obj);
    RUNTIME_ERROR("cannot 'setattr()' objects of type: '%s'",
                  get_object_type(&obj));
  }

  Object *target =
      table_get(AS_STRUCT(obj)->properties, code->sp.data[property_name_idx]);
  if (target) {
    objdecref(target);
  }

  table_insert(AS_STRUCT(obj)->properties, code->sp.data[property_name_idx],
               value);

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
static inline void handle_op_getattr(VM *restrict vm, const Bytecode *restrict code,
                                     uint8_t *restrict *ip)
{
  uint8_t property_name_idx = READ_UINT8();

  Object obj = pop(vm);

  if (!IS_STRUCT(obj)) {
    objdecref(&obj);
    RUNTIME_ERROR("cannot 'getattr()' objects of type: '%s'",
                  get_object_type(&obj));
  }

  Object *property =
      table_get(AS_STRUCT(obj)->properties, code->sp.data[property_name_idx]);
  if (!property) {
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
static inline void handle_op_getattr_ptr(VM *restrict vm, const Bytecode *restrict code,
                                         uint8_t *restrict *ip)
{
  uint8_t property_name_idx = READ_UINT8();

  Object object = pop(vm);

  StructBlueprint *sb =
      table_get_unchecked(vm->blueprints, AS_STRUCT(object)->name);

  int *idx = table_get(sb->property_indexes, code->sp.data[property_name_idx]);
  if (!idx) {
    RUNTIME_ERROR("Property '%s' is not defined on struct '%s'.",
                  code->sp.data[property_name_idx], AS_STRUCT(object)->name);
  }

  Object *property = table_get(AS_STRUCT(object)->properties,
                               code->sp.data[property_name_idx]);

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
static inline void handle_op_struct(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  uint8_t structname = READ_UINT8();

  StructBlueprint *sb = table_get(vm->blueprints, code->sp.data[structname]);
  if (!sb) {
    RUNTIME_ERROR("struct '%s' is not defined", code->sp.data[structname]);
  }

  Struct s = {.name = code->sp.data[structname],
              .propcount = sb->property_indexes->count,
              .refcount = 1,
              .properties = calloc(1, sizeof(Table_Object))};

  for (size_t i = 0; i < sb->methods->count; i++) {
    Function f = {
        .location = sb->methods->items[i]->location,
        .name = sb->methods->items[i]->name,
        .paramcount = sb->methods->items[i]->paramcount,
        .upvalue_count = 0,
    };

    Closure c = {
        .func = ALLOC(f), .refcount = 1, .upvalue_count = 0, .upvalues = NULL};

    table_insert(s.properties, sb->methods->items[i]->name,
                 CLOSURE_VAL(ALLOC(c)));
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
static inline void handle_op_struct_blueprint(VM *vm,
                                              const Bytecode *restrict code,
                                              uint8_t *restrict *ip)
{
  uint8_t name_idx = READ_UINT8();
  uint8_t propcount = READ_UINT8();

  DynArray_char_ptr properties = {0};
  DynArray_uint8_t prop_indexes = {0};
  for (size_t i = 0; i < propcount; i++) {
    dynarray_insert(&properties, code->sp.data[READ_UINT8()]);
    dynarray_insert(&prop_indexes, READ_UINT8());
  }

  StructBlueprint sb = {.name = code->sp.data[name_idx],
                        .property_indexes = calloc(1, sizeof(Table_int)),
                        .methods = calloc(1, sizeof(Table_Function))};

  for (size_t i = 0; i < properties.count; i++) {
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
static inline void handle_op_impl(VM *restrict vm, const Bytecode *restrict code,
                                  uint8_t *restrict *ip)
{
  uint8_t blueprint_name_idx = READ_UINT8();
  uint8_t method_count = READ_UINT8();

  StructBlueprint *sb =
      table_get(vm->blueprints, code->sp.data[blueprint_name_idx]);
  if (!sb) {
    RUNTIME_ERROR("struct '%s' is not defined",
                  code->sp.data[blueprint_name_idx]);
  }

  for (size_t i = 0; i < method_count; i++) {
    uint8_t method_name_idx = READ_UINT8();
    uint8_t paramcount = READ_UINT8();
    uint8_t location = READ_UINT8();

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

  while (current && current->location > local) {
    prev = current;
    current = current->next;
  }

  if (current && current->location == local) {
    return current;
  }

  Upvalue *created = new_upvalue(local);
  created->next = current;

  if (!prev) {
    vm->upvalues = created;
  } else {
    prev->next = created;
  }

  return created;
}

static void close_upvalues(VM *vm, Object *last)
{
  while (vm->upvalues && vm->upvalues->location >= last) {
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
static inline void handle_op_closure(VM *restrict vm, const Bytecode *restrict code,
                                     uint8_t *restrict *ip)
{
  uint8_t name_idx, paramcount, location, upvalue_count;

  Function f;
  Closure c;

  name_idx = READ_UINT8();
  paramcount = READ_UINT8();
  location = READ_UINT8();
  upvalue_count = READ_UINT8();

  f = (Function){.name = code->sp.data[name_idx],
                 .paramcount = paramcount,
                 .location = location,
                 .upvalue_count = upvalue_count};

  c = (Closure){
      .upvalues = malloc(sizeof(Upvalue *) * f.upvalue_count),
      .upvalue_count = f.upvalue_count,
      .refcount = 1,
      .func = ALLOC(f),
  };

  for (int i = 0; i < c.upvalue_count; i++) {
    uint8_t idx = READ_UINT8();
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
static inline void handle_op_call(VM *restrict vm, const Bytecode *restrict code,
                                  uint8_t *restrict *ip)
{
  uint8_t argcount = READ_UINT8();

  Object obj = pop(vm);
  objdecref(&obj);

  Closure *f = AS_CLOSURE(obj);

  BytecodePtr ip_obj = {.addr = *(ip), .location = vm->tos - argcount, .fn = f};
  push_frame(vm, ip_obj);

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
static inline void handle_op_call_method(VM *restrict vm, const Bytecode *restrict code,
                                         uint8_t *restrict *ip)
{
  uint8_t method_name_idx = READ_UINT8();
  uint8_t argcount = READ_UINT8();

  Object object = peek(vm, argcount);

  if (!IS_STRUCT(object)) {
    RUNTIME_ERROR("cannot call objects of type: '%s'",
                  get_object_type(&object));
  }

  /* Look up the method with that name on the blueprint. */
  Object *methodobj =
      table_get(AS_STRUCT(object)->properties, code->sp.data[method_name_idx]);

  if (!methodobj) {
    RUNTIME_ERROR("method '%s' is not defined on struct: '%s'",
                  code->sp.data[method_name_idx], AS_STRUCT(object)->name);
  }

  Closure *c = AS_CLOSURE(*methodobj);

  /* Push the instruction pointer on the frame ptr stack.
   * No need to take into account the jump sequence (+3). */
  BytecodePtr ip_obj = {
      .addr = *ip, .location = vm->tos - c->func->paramcount, .fn = c};
  push_frame(vm, ip_obj);

  /* Direct jump to one byte before the method location. */
  *ip = &code->code.data[c->func->location - 1];
}

static void scheduler_complete_current(VM *restrict vm, const Bytecode *restrict code,
                                       uint8_t *restrict *ip, Object returned);

/* OP_RET pops a BytecodePtr off the frame pointer stack
 * and sets the instruction pointer to point to the add-
 * ress contained in the BytecodePtr. */
static inline void handle_op_ret(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  if (vm->scheduler_running && vm->current_task) {
    Generator *gen = vm->current_task->gen;
    if (vm->fp_count > 0 && vm->fp_stack[vm->fp_count - 1].fn != gen->fn) {
      BytecodePtr ptr = pop_frame(vm);
      *ip = ptr.addr;
    }

    Object returned = pop(vm);
    scheduler_complete_current(vm, code, ip, returned);
    return;
  }

  (void) code;

  if (vm->gen_count > 0) {
    Generator *gen = vm->gen_stack[vm->gen_count - 1];
    if (vm->fp_count > 0 && vm->fp_stack[vm->fp_count - 1].fn != gen->fn) {
      BytecodePtr ptr = pop_frame(vm);
      *ip = ptr.addr;
    }

    --vm->gen_count;
    FrameSnapshot *fs = vm->fs_stack[--vm->fs_count];

    Object returned = pop(vm);

    memcpy(vm->stack, fs->stack, sizeof(Object) * fs->tos);
    vm->tos = fs->tos;

    memcpy(vm->fp_stack, fs->fp_stack, sizeof(BytecodePtr) * fs->fp_count);
    vm->fp_count = fs->fp_count;

    *ip = gen->ip;
    gen->state = STATE_DONE;
    gen->tos = 0;
    gen->fp_count = 0;

    push(vm, returned);

    free(fs);

    Object gen_obj = GENERATOR_VAL(gen);
    objdecref(&gen_obj);

    return;
  }

  BytecodePtr ptr = pop_frame(vm);
  *ip = ptr.addr;
  vm->fp_base = vm->fp_stack[vm->fp_count - 1].location;
}

/* OP_POP pops an object off the stack.
 *
 * REFCOUNTING: Since the popped object might be refcounted,
 * its refcount must be decremented. */
static inline void handle_op_pop(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  Object obj = pop(vm);
  objdecref(&obj);
}

/* OP_DEREF pops an object off the stack, dereferences it
 * and pushes it back on the stack.
 *
 * REFCOUNTING: Since the object will now be present in one
 * more another location, its refcount must be incremented. */
static inline void handle_op_deref(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
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
static inline void handle_op_strcat(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  Object b = pop(vm);
  Object a = pop(vm);

  if (IS_STRING(a) && IS_STRING(b)) {
    char *result =
        concatenate_strings(AS_STRING(a)->value, AS_STRING(b)->value);

    String s = {.refcount = 1, .value = result};

    push(vm, STRING_VAL(ALLOC(s)));

    objdecref(&b);
    objdecref(&a);
  } else {
    objdecref(&b);
    objdecref(&a);

    RUNTIME_ERROR("cannot '++' objects of types: '%s' and '%s'",
                  get_object_type(&a), get_object_type(&b));
  }
}

/* OP_ARRAY reads a 4-byte count of the array elements, pops that many ele-
 * ments off the stack, inserts them into a dynarray, creates an Array obj-
 * ect, and pushes it on the stack.
 *
 * REFCOUNTING: Since Arrays are refcounted, the new object has refcount=1. */
static inline void handle_op_array(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
{
  uint8_t count = READ_UINT8();

  DynArray_Object elements = {0};
  for (size_t i = 0; i < count; i++) {
    dynarray_insert(&elements, pop(vm));
  }

  Array array = {.refcount = 1, .elements = elements};
  push(vm, ARRAY_VAL(ALLOC(array)));
}

/* OP_ARRAYSET pops three objects off the stack: the index, the array object,
 * and the value. Then, it reassigns the element within the array at that idx
 * to 'value'.
 *
 * REFCOUNTING: We need to make sure to decrement the refcount for the popped
 * array, since arrays are refcounted objects. */
static inline void handle_op_arrayset(VM *restrict vm, const Bytecode *restrict code,
                                      uint8_t *restrict *ip)
{
  Object value = pop(vm);
  Object index = pop(vm);
  Object subscriptee = pop(vm);

  if (!IS_ARRAY(subscriptee)) {
    objdecref(&value);
    objdecref(&index);
    objdecref(&subscriptee);

    RUNTIME_ERROR("cannot '[]' objects of type: '%s'",
                  get_object_type(&subscriptee));
  }

  if (!IS_NUM(index)) {
    objdecref(&value);
    objdecref(&index);
    objdecref(&subscriptee);

    RUNTIME_ERROR("array index must be a number, got: '%s'",
                  get_object_type(&index));
  }

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
static inline void handle_op_subscript(VM *restrict vm, const Bytecode *restrict code,
                                       uint8_t *restrict *ip)
{
  Object index = pop(vm);
  Object object = pop(vm);

  if (!IS_ARRAY(object)) {
    objdecref(&index);
    objdecref(&object);
    RUNTIME_ERROR("cannot '[]' objects of type: '%s'",
                  get_object_type(&object));
  }

  if (!IS_NUM(index)) {
    objdecref(&index);
    objdecref(&object);
    RUNTIME_ERROR("array index must be a number, got: '%s'",
                  get_object_type(&index));
  }

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
static inline void handle_op_get_upvalue(VM *restrict vm, const Bytecode *restrict code,
                                         uint8_t *restrict *ip)
{
  uint8_t idx = READ_UINT8();

  Object *obj = vm->fp_stack[vm->fp_count - 1].fn->upvalues[idx]->location;
  objincref(obj);
  push(vm, *obj);
}

/* OP_GET_UPVALUE_PTR reads a 4-byte index of the upvalue and pushes it
 * on the stack. It's exactly like OP_GET_UPVALUE, differing in that it
 * pushes /the address/ of the object instead of the object itself. */
static inline void handle_op_get_upvalue_ptr(VM *vm,
                                             const Bytecode *restrict code,
                                             uint8_t *restrict *ip)
{
  uint8_t idx = READ_UINT8();

  Object *obj = vm->fp_stack[vm->fp_count - 1].fn->upvalues[idx]->location;
  push(vm, PTR_VAL(obj));
}

/* OP_SET_UPVALUE reads a 4-byte index of the upvalue and sets it to the
 * previously popped object.
 *
 * REFCOUNTING: Since the target value will now be gone from that place,
 * we need to make sure to decrement its refcount. */
static inline void handle_op_set_upvalue(VM *restrict vm, const Bytecode *restrict code,
                                         uint8_t *restrict *ip)
{
  uint8_t idx = READ_UINT8();

  Object obj = pop(vm);

  objdecref(vm->fp_stack[vm->fp_count - 1].fn->upvalues[idx]->location);
  *vm->fp_stack[vm->fp_count - 1].fn->upvalues[idx]->location = obj;
}

/* OP_CLOSE_UPVALUE is a part of the stack cleanup procedure and runs upon
 * returning from the function. The push/pop dance is to preserve the ret-
 * urn value. */
static inline void handle_op_close_upvalue(VM *vm,
                                           const Bytecode *restrict code,
                                           uint8_t *restrict *ip)
{
  Object result = pop(vm);

  close_upvalues(vm, &vm->stack[vm->tos - 1]);
  pop(vm);
  push(vm, result);
}

static FrameSnapshot *snapshot_frame(VM *vm, uint8_t *ip)
{
  FrameSnapshot fs = {.tos = vm->tos, .ip = ip, .fp_count = vm->fp_count};
  memcpy(fs.stack, vm->stack, sizeof(Object) * vm->tos);
  memcpy(fs.fp_stack, vm->fp_stack, sizeof(BytecodePtr) * vm->fp_count);
  return ALLOC(fs);
}

static void restore_frame(VM *vm, FrameSnapshot *fs, uint8_t *restrict *ip)
{
  memcpy(vm->stack, fs->stack, sizeof(Object) * fs->tos);
  vm->tos = fs->tos;
  memcpy(vm->fp_stack, fs->fp_stack, sizeof(BytecodePtr) * fs->fp_count);
  vm->fp_count = fs->fp_count;
  *ip = fs->ip;
  free(fs);
}

static Task *vm_create_task(VM *vm, Generator *gen)
{
  if (vm->task_count >= STACK_MAX) {
    return NULL;
  }

  Object gen_obj = GENERATOR_VAL(gen);
  objincref(&gen_obj);

  Task task = {.refcount = 1,
               .id = ++vm->next_task_id,
               .gen = gen,
               .done = false,
               .has_result = false,
               .result = NULL_VAL,
               .waiting_on = NULL,
               .wake_tick = vm->scheduler_tick,
               .has_send = false,
               .send_value = NULL_VAL};

  Task *task_ptr = ALLOC(task);
  vm->tasks[vm->task_count++] = task_ptr;
  return task_ptr;
}

static void task_set_send_move(Task *task, Object value)
{
  if (task->has_send) {
    objdecref(&task->send_value);
  }
  task->send_value = value;
  task->has_send = true;
}

static void task_set_send_copy(Task *task, Object value)
{
  objincref(&value);
  task_set_send_move(task, value);
}

static void scheduler_unblock_waiters(VM *vm, Task *finished)
{
  for (size_t i = 0; i < vm->task_count; i++) {
    Task *task = vm->tasks[i];
    if (!task->done && task->waiting_on == finished) {
      task->waiting_on = NULL;
      if (finished->has_result) {
        task_set_send_copy(task, finished->result);
      } else {
        task_set_send_move(task, NULL_VAL);
      }
    }
  }
}

static bool scheduler_has_live_tasks(VM *vm)
{
  for (size_t i = 0; i < vm->task_count; i++) {
    if (!vm->tasks[i]->done) {
      return true;
    }
  }
  return false;
}

static bool scheduler_task_runnable(VM *vm, Task *task)
{
  return !task->done && task->waiting_on == NULL &&
         task->wake_tick <= vm->scheduler_tick;
}

static Task *scheduler_next_runnable(VM *vm, bool *deadlocked)
{
  *deadlocked = false;

  for (;;) {
    for (size_t n = 0; n < vm->task_count; n++) {
      size_t i = (vm->scheduler_cursor + n) % vm->task_count;
      Task *task = vm->tasks[i];
      if (scheduler_task_runnable(vm, task)) {
        vm->scheduler_cursor = (i + 1) % vm->task_count;
        return task;
      }
    }

    bool live = false;
    bool found_timer = false;
    int next_wake = 0;
    for (size_t i = 0; i < vm->task_count; i++) {
      Task *task = vm->tasks[i];
      if (task->done) {
        continue;
      }
      live = true;
      if (task->waiting_on == NULL && task->wake_tick > vm->scheduler_tick) {
        if (!found_timer || task->wake_tick < next_wake) {
          found_timer = true;
          next_wake = task->wake_tick;
        }
      }
    }

    if (!live) {
      return NULL;
    }

    if (found_timer) {
      vm->scheduler_tick = next_wake;
      continue;
    }

    *deadlocked = true;
    return NULL;
  }
}

static void scheduler_resume_task(VM *restrict vm, const Bytecode *restrict code,
                                  uint8_t *restrict *ip, Task *task)
{
  (void) code;

  Generator *gen = task->gen;
  if (gen->state == STATE_DONE) {
    task->done = true;
  }

  memcpy(vm->stack, gen->stack, sizeof(Object) * gen->tos);
  vm->tos = gen->tos;
  memcpy(vm->fp_stack, gen->fp_stack, sizeof(BytecodePtr) * gen->fp_count);
  vm->fp_count = gen->fp_count;

  if (gen->state == STATE_NEW) {
    BytecodePtr ptr = {.addr = *ip, .location = 0, .fn = gen->fn};
    vm->fp_stack[vm->fp_count++] = ptr;
  } else if (gen->state == STATE_SUSPENDED) {
    Object sent = task->has_send ? task->send_value : NULL_VAL;
    task->has_send = false;
    push(vm, sent);
  }

  *ip = gen->ip;
  gen->state = STATE_ACTIVE;
  vm->current_task = task;
  vm->gen_stack[vm->gen_count++] = gen;
}

static void scheduler_finish(VM *restrict vm, const Bytecode *restrict code,
                             uint8_t *restrict *ip)
{
  (void) code;

  Object result = NULL_VAL;
  if (vm->scheduler_root && vm->scheduler_root->has_result) {
    result = vm->scheduler_root->result;
    objincref(&result);
  }

  restore_frame(vm, vm->scheduler_frame, ip);
  vm->scheduler_frame = NULL;
  vm->scheduler_running = false;
  vm->current_task = NULL;
  vm->scheduler_root = NULL;
  push(vm, result);
}

static void scheduler_schedule_next(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  bool deadlocked = false;
  Task *next = scheduler_next_runnable(vm, &deadlocked);
  if (next) {
    scheduler_resume_task(vm, code, ip, next);
    return;
  }

  if (deadlocked) {
    RUNTIME_ERROR("scheduler deadlock: no runnable tasks");
  }

  scheduler_finish(vm, code, ip);
}

static void scheduler_process_awaited(VM *restrict vm, const Bytecode *restrict code,
                                      uint8_t *restrict *ip, Object awaited)
{
  Task *task = vm->current_task;
  if (IS_SLEEP(awaited)) {
    int ticks = AS_SLEEP(awaited)->ticks;
    if (ticks < 0) {
      ticks = 0;
    }
    task->wake_tick = vm->scheduler_tick + ticks;
    task_set_send_move(task, NULL_VAL);
    objdecref(&awaited);
  } else if (IS_TASK(awaited)) {
    Task *other = AS_TASK(awaited);
    if (other->done) {
      if (other->has_result) {
        task_set_send_copy(task, other->result);
      } else {
        task_set_send_move(task, NULL_VAL);
      }
    } else {
      task->waiting_on = other;
    }
    objdecref(&awaited);
  } else if (IS_GENERATOR(awaited)) {
    Task *child = vm_create_task(vm, AS_GENERATOR(awaited));
    if (!child) {
      objdecref(&awaited);
      RUNTIME_ERROR("scheduler task limit exceeded");
    }
    task->waiting_on = child;
    objdecref(&awaited);
  } else {
    task_set_send_move(task, awaited);
  }

  scheduler_schedule_next(vm, code, ip);
}

static void scheduler_suspend_current(VM *restrict vm, const Bytecode *restrict code,
                                      uint8_t *restrict *ip, Object awaited)
{
  if (!vm->current_task) {
    objdecref(&awaited);
    RUNTIME_ERROR("scheduler has no current task");
  }

  Generator *gen = vm->current_task->gen;
  if (vm->gen_count > 0) {
    --vm->gen_count;
  }

  memcpy(gen->stack, vm->stack, sizeof(Object) * vm->tos);
  gen->tos = vm->tos;
  memcpy(gen->fp_stack, vm->fp_stack, sizeof(BytecodePtr) * vm->fp_count);
  gen->fp_count = vm->fp_count;
  gen->ip = *ip;
  gen->state = STATE_SUSPENDED;

  scheduler_process_awaited(vm, code, ip, awaited);
}

static void scheduler_complete_current(VM *restrict vm, const Bytecode *restrict code,
                                       uint8_t *restrict *ip, Object returned)
{
  Task *task = vm->current_task;
  if (!task) {
    objdecref(&returned);
    RUNTIME_ERROR("scheduler has no current task");
  }

  if (vm->gen_count > 0) {
    --vm->gen_count;
  }

  Generator *gen = task->gen;
  gen->state = STATE_DONE;
  gen->tos = 0;
  gen->fp_count = 0;

  if (task->has_result) {
    objdecref(&task->result);
  }
  task->result = returned;
  task->has_result = true;
  task->done = true;
  task->waiting_on = NULL;

  scheduler_unblock_waiters(vm, task);
  scheduler_schedule_next(vm, code, ip);
}

/* OP_MKGEN pops the closure off of the stack, makes a generator object
 * out of it, and pushes it on the stack.
 *
 * REFCOUNTING: Since the popped object is a closure and therefore ref-
 * counted, it is necessary to decrement the refcount. */
static inline void handle_op_mkgen(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
{
  Object closure = pop(vm);
  Closure *closure_ptr = AS_CLOSURE(closure);
  size_t paramcount = closure_ptr->func->paramcount;

  Generator gen = {.refcount = 1,
                   .fn = closure_ptr,
                   .tos = 0,
                   .fp_count = 0,
                   .state = STATE_NEW};

  if (paramcount > vm->tos) {
    objdecref(&closure);
    RUNTIME_ERROR("not enough arguments to create generator");
  }

  size_t arg_base = vm->tos - paramcount;
  for (size_t i = 0; i < paramcount; i++) {
    gen.stack[i] = vm->stack[arg_base + i];
  }
  gen.tos = paramcount;
  vm->tos -= paramcount;

  gen.ip = &code->code.data[closure_ptr->func->location - 1];

  push(vm, GENERATOR_VAL(ALLOC(gen)));
  objdecref(&closure);
}

/* OP_YIELD takes a snapshot of the running generator's stacks (main stack
 * and frame pointer stack), and then restores the caller's context onto the
 * main VM stack.  Finally, it continues the execution from where the caller
 * was suspended. */
static inline void handle_op_yield(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
{
  Object yielded = pop(vm);

  if (vm->scheduler_running) {
    scheduler_suspend_current(vm, code, ip, yielded);
    return;
  }

  Generator *gen = vm->gen_stack[--vm->gen_count];
  FrameSnapshot *fs = vm->fs_stack[--vm->fs_count];

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

  Object gen_obj = GENERATOR_VAL(gen);
  objdecref(&gen_obj);
}

static inline void resume_generator(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip, Object obj,
                                    Object sent)
{
  (void) code;

  if (!IS_GENERATOR(obj)) {
    const char *type_name = get_object_type(&obj);
    objdecref(&obj);
    objdecref(&sent);
    RUNTIME_ERROR("cannot resume objects of type: '%s'", type_name);
  }

  Generator *gen = AS_GENERATOR(obj);

  if (gen->state == STATE_DONE) {
    objdecref(&obj);
    objdecref(&sent);
    RUNTIME_ERROR("cannot resume a completed generator");
  }

  if (gen->state == STATE_NEW && !IS_NULL(sent)) {
    objdecref(&obj);
    objdecref(&sent);
    RUNTIME_ERROR("can't send non-null value to a just-started generator");
  }

  FrameSnapshot fs = {.tos = vm->tos, .ip = *ip, .fp_count = vm->fp_count};

  memcpy(fs.stack, vm->stack, sizeof(Object) * vm->tos);
  memcpy(fs.fp_stack, vm->fp_stack, sizeof(BytecodePtr) * vm->fp_count);

  vm->fs_stack[vm->fs_count++] = ALLOC(fs);

  memcpy(vm->stack, gen->stack, sizeof(Object) * gen->tos);
  vm->tos = gen->tos;

  memcpy(vm->fp_stack, gen->fp_stack, sizeof(BytecodePtr) * gen->fp_count);
  vm->fp_count = gen->fp_count;

  if (gen->state == STATE_NEW) {
    BytecodePtr ptr = {.addr = *ip, .fn = gen->fn, .location = 0};
    vm->fp_stack[vm->fp_count++] = ptr;
  }

  if (gen->state == STATE_SUSPENDED) {
    push(vm, sent);
  } else {
    objdecref(&sent);
  }

  uint8_t *tmp_ip = *ip;
  *ip = gen->ip;
  gen->ip = tmp_ip;

  gen->state = STATE_ACTIVE;

  objincref(&obj);
  vm->gen_stack[vm->gen_count++] = gen;

  objdecref(&obj);
}

/* OP_RESUME implements next(gen).  It resumes a generator without sending a
 * value back into the suspended yield expression. */
static inline void handle_op_resume(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  Object obj = pop(vm);
  return resume_generator(vm, code, ip, obj, NULL_VAL);
}

/* OP_SEND implements send(gen, value).  The value becomes the result of the
 * suspended yield expression inside the generator. */
static inline void handle_op_send(VM *restrict vm, const Bytecode *restrict code,
                                  uint8_t *restrict *ip)
{
  Object sent = pop(vm);
  Object obj = pop(vm);
  return resume_generator(vm, code, ip, obj, sent);
}

/* OP_AWAIT suspends the current async task and lets the scheduler decide
 * when and with what value it should be resumed. */
static inline void handle_op_await(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
{
  Object awaited = pop(vm);
  if (!vm->scheduler_running) {
    objdecref(&awaited);
    RUNTIME_ERROR("'await' requires run(...) scheduler context");
  }

  scheduler_suspend_current(vm, code, ip, awaited);
}

static inline void handle_op_spawn(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
{
  (void) code;
  (void) ip;

  Object obj = pop(vm);
  if (!IS_GENERATOR(obj)) {
    const char *type_name = get_object_type(&obj);
    objdecref(&obj);
    RUNTIME_ERROR("spawn(...) requires an async coroutine/generator, got '%s'",
                  type_name);
  }

  Task *task = vm_create_task(vm, AS_GENERATOR(obj));
  objdecref(&obj);
  if (!task) {
    RUNTIME_ERROR("scheduler task limit exceeded");
  }

  Object task_obj = TASK_VAL(task);
  objincref(&task_obj);
  push(vm, task_obj);
}

static inline void handle_op_run(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  if (vm->scheduler_running) {
    RUNTIME_ERROR("run(...) cannot be nested inside a running scheduler");
  }

  Object obj = pop(vm);
  Task *root = NULL;

  if (IS_GENERATOR(obj)) {
    root = vm_create_task(vm, AS_GENERATOR(obj));
    objdecref(&obj);
    if (!root) {
      RUNTIME_ERROR("scheduler task limit exceeded");
    }
  } else if (IS_TASK(obj)) {
    root = AS_TASK(obj);
    objdecref(&obj);
  } else {
    const char *type_name = get_object_type(&obj);
    objdecref(&obj);
    RUNTIME_ERROR(
        "run(...) requires an async coroutine/generator or task, got '%s'",
        type_name);
  }

  vm->scheduler_running = true;
  vm->scheduler_root = root;
  vm->current_task = NULL;
  vm->scheduler_frame = snapshot_frame(vm, *ip);
  vm->scheduler_cursor = 0;

  if (!scheduler_has_live_tasks(vm)) {
    scheduler_finish(vm, code, ip);
    return;
  }

  scheduler_schedule_next(vm, code, ip);
}

static inline void handle_op_sleep(VM *restrict vm, const Bytecode *restrict code,
                                   uint8_t *restrict *ip)
{
  (void) code;
  (void) ip;

  Object obj = pop(vm);
  if (!IS_NUM(obj)) {
    const char *type_name = get_object_type(&obj);
    objdecref(&obj);
    RUNTIME_ERROR("sleep(...) requires a number of scheduler ticks, got '%s'",
                  type_name);
  }

  int ticks = (int) AS_NUM(obj);
  if (ticks < 0) {
    ticks = 0;
  }
  Sleep sleep = {.refcount = 1, .ticks = ticks};
  push(vm, SLEEP_VAL(ALLOC(sleep)));
}

static inline void handle_op_done(VM *restrict vm, const Bytecode *restrict code,
                                  uint8_t *restrict *ip)
{
  (void) code;
  (void) ip;

  Object obj = pop(vm);
  if (!IS_TASK(obj)) {
    const char *type_name = get_object_type(&obj);
    objdecref(&obj);
    RUNTIME_ERROR("done(...) requires a task, got '%s'", type_name);
  }

  bool done = AS_TASK(obj)->done;
  objdecref(&obj);
  push(vm, BOOL_VAL(done));
}

static inline void handle_op_result(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  (void) code;
  (void) ip;

  Object obj = pop(vm);
  if (!IS_TASK(obj)) {
    const char *type_name = get_object_type(&obj);
    objdecref(&obj);
    RUNTIME_ERROR("result(...) requires a task, got '%s'", type_name);
  }

  Task *task = AS_TASK(obj);
  if (!task->done) {
    objdecref(&obj);
    RUNTIME_ERROR("result(...) cannot read a pending task");
  }

  Object result = task->has_result ? task->result : NULL_VAL;
  objincref(&result);
  objdecref(&obj);
  push(vm, result);
}

static inline void handle_op_len(VM *restrict vm, const Bytecode *restrict code,
                                 uint8_t *restrict *ip)
{
  Object obj = pop(vm);

  if (IS_STRING(obj)) {
    push(vm, NUM_VAL(strlen(AS_STRING(obj)->value)));
  } else if (IS_ARRAY(obj)) {
    push(vm, NUM_VAL(AS_ARRAY(obj)->elements.count));
  } else {
    objdecref(&obj);
    RUNTIME_ERROR("cannot 'len()' objects of type: '%s'",
                  get_object_type(&obj));
  }

  objdecref(&obj);
}

static inline void handle_op_hasattr(VM *restrict vm, const Bytecode *restrict code,
                                     uint8_t *restrict *ip)
{
  Object attr = pop(vm);
  Object obj = pop(vm);

  if (!IS_STRUCT(obj)) {
    objdecref(&obj);
    objdecref(&attr);

    RUNTIME_ERROR("cannot 'hasattr()' objects of type: '%s'",
                  get_object_type(&obj));
  }

  Object *found = table_get(AS_STRUCT(obj)->properties, AS_STRING(attr)->value);
  push(vm, !found ? BOOL_VAL(false) : BOOL_VAL(true));

  objdecref(&obj);
  objdecref(&attr);
}

static inline void handle_op_assert(VM *restrict vm, const Bytecode *restrict code,
                                    uint8_t *restrict *ip)
{
  Object assertion = pop(vm);

  if (!IS_BOOL(assertion)) {
    objdecref(&assertion);
    RUNTIME_ERROR("cannot 'assert()' objects of type: '%s'",
                  get_object_type(&assertion));
  }

  if (!AS_BOOL(assertion)) {
    objdecref(&assertion);
    RUNTIME_ERROR("assertion failed");
  }
}

#ifdef venom_debug_vm
static inline const char *print_current_instruction(uint8_t opcode)
{
  return disassemble_handler[opcode].opcode;
}
#endif

ExecResult exec(VM *restrict vm, const Bytecode *code)
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
      &&op_send,
      &&op_await,
      &&op_spawn,
      &&op_run,
      &&op_sleep,
      &&op_done,
      &&op_result,
      &&op_len,
      &&op_hasattr,
      &&op_assert,
      &&op_hlt,
  };

#ifndef venom_debug_vm
#define DISPATCH() goto *dispatch_table[*++ip]
#else
#define DISPATCH()                                                         \
  do {                                                                     \
    PRINT_STACK();                                                         \
    PRINT_FPSTACK();                                                       \
    printf("%ld: ", ip - code->code.data + 1);                             \
    printf("current instruction: %s\n", print_current_instruction(*++ip)); \
    goto *dispatch_table[*ip];                                             \
  } while (0)
#endif

#define HANDLE(name)                           \
  op_##name : handle_op_##name(vm, code, &ip); \
  DISPATCH();

  ExecResult r = {.is_ok = true, .errcode = 0, .msg = NULL, .time = 0.0};

  struct timespec start, end;

  clock_gettime(CLOCK_MONOTONIC, &start);

  int status = setjmp(vm->trap);
  if (status != 0) {
    goto bail;
  }

  uint8_t *restrict ip = code->code.data;

  goto *dispatch_table[*ip];

  HANDLE(print)
  HANDLE(add)
  HANDLE(sub)
  HANDLE(mul)
  HANDLE(div)
  HANDLE(mod)
  HANDLE(eq)
  HANDLE(gt)
  HANDLE(lt)
  HANDLE(not )
  HANDLE(neg)
  HANDLE(true)
  HANDLE(null)
  HANDLE(const)
  HANDLE(str)
  HANDLE(jmp)
  HANDLE(jz)
  HANDLE(bitand)
  HANDLE(bitor)
  HANDLE(bitxor)
  HANDLE(bitnot)
  HANDLE(bitshl)
  HANDLE(bitshr)
  HANDLE(set_global)
  HANDLE(get_global)
  HANDLE(get_global_ptr)
  HANDLE(deepset)
  HANDLE(deepget)
  HANDLE(deepget_ptr)
  HANDLE(setattr)
  HANDLE(getattr)
  HANDLE(getattr_ptr)
  HANDLE(struct)
  HANDLE(struct_blueprint)
  HANDLE(closure)
  HANDLE(call)
  HANDLE(call_method)
  HANDLE(ret)
  HANDLE(pop)
  HANDLE(deref)
  HANDLE(derefset)
  HANDLE(strcat)
  HANDLE(array)
  HANDLE(arrayset)
  HANDLE(subscript)
  HANDLE(get_upvalue)
  HANDLE(get_upvalue_ptr)
  HANDLE(set_upvalue)
  HANDLE(close_upvalue)
  HANDLE(impl)
  HANDLE(mkgen)
  HANDLE(yield)
  HANDLE(resume)
  HANDLE(send)
  HANDLE(await)
  HANDLE(spawn)
  HANDLE(run)
  HANDLE(sleep)
  HANDLE(done)
  HANDLE(result)
  HANDLE(len)
  HANDLE(hasattr)
  HANDLE(assert)

op_hlt:
  assert(vm->tos == 0);
  clock_gettime(CLOCK_MONOTONIC, &end);
  r.time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  return r;

bail:
  clock_gettime(CLOCK_MONOTONIC, &end);
  r.time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  r.is_ok = false;
  r.errcode = -1;
  r.msg = vm->err_msg;
  return r;

#undef HANDLE
#undef DISPATCH
}
