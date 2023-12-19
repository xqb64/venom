#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "disassembler.h"
#include "dynarray.h"
#include "math.h"
#include "table.h"
#include "util.h"
#include "vm.h"

void init_vm(VM *vm) {
  memset(vm, 0, sizeof(VM));
  vm->blueprints = calloc(1, sizeof(Table_StructBlueprint));
}

void free_vm(VM *vm) {
  free_table_object(&vm->globals);
  free_table_struct_blueprints(vm->blueprints);
  free(vm->blueprints);
}

static inline void push(VM *vm, Object obj) { vm->stack[vm->tos++] = obj; }

static inline Object pop(VM *vm) { return vm->stack[--vm->tos]; }

static inline uint64_t clamp(double d) {
  if (d < 0.0) {
    return 0;
  } else if (d > UINT64_MAX) {
    return UINT64_MAX;
  } else {
    return (uint64_t)d;
  }
}

#define BINARY_OP(op, wrapper)                                                 \
  do {                                                                         \
    Object b = pop(vm);                                                        \
    Object a = pop(vm);                                                        \
    Object obj = wrapper(AS_NUM(a) op AS_NUM(b));                              \
    push(vm, obj);                                                             \
  } while (0)

#define BITWISE_OP(op)                                                         \
  do {                                                                         \
    Object b = pop(vm);                                                        \
    Object a = pop(vm);                                                        \
                                                                               \
    uint64_t clamped_a = clamp(AS_NUM(a));                                     \
    uint64_t clamped_b = clamp(AS_NUM(b));                                     \
                                                                               \
    uint64_t result = clamped_a op clamped_b;                                  \
                                                                               \
    Object obj = NUM_VAL((double)result);                                      \
                                                                               \
    push(vm, obj);                                                             \
  } while (0)

#define READ_UINT8() (*++(*ip))

#define READ_INT16()                                                           \
  /* ip points to one of the jump instructions and                             \
   * there is a 2-byte operand (offset) that comes                             \
   * after the opcode. The instruction pointer ne-                             \
   * eds to be incremented to point to the last of                             \
   * the two operands, and a 16-bit offset constr-                             \
   * ucted from the two bytes. Then the instructi-                             \
   * on pointer will be incremented in by the main                             \
   * loop again and will point to the next instru-                             \
   * ction that comes after the jump. */                                       \
  (*ip += 2, (int16_t)(((*ip)[-1] << 8) | (*ip)[0]))

#define READ_UINT32()                                                          \
  (*ip += 4, (uint32_t)(((*ip)[-3] << 24) | ((*ip)[-2] << 16) |                \
                        ((*ip)[-1] << 8) | (*ip)[0]))

#define PRINT_STACK()                                                          \
  do {                                                                         \
    printf("stack: [");                                                        \
    for (size_t i = 0; i < vm->tos; i++) {                                     \
      print_object(&vm->stack[i]);                                             \
      if (i < vm->tos - 1) {                                                   \
        printf(", ");                                                          \
      }                                                                        \
    }                                                                          \
    printf("]\n");                                                             \
  } while (0)

#define PRINT_FPSTACK()                                                        \
  do {                                                                         \
    printf("fp stack: [");                                                     \
    for (size_t i = 0; i < vm->fp_count; i++) {                                \
      printf("ptr (loc: %d)", vm->fp_stack[i].location);                       \
      if (i < vm->fp_count - 1) {                                              \
        printf(", ");                                                          \
      }                                                                        \
    }                                                                          \
    printf("]\n");                                                             \
  } while (0)

#define RUNTIME_ERROR(...)                                                     \
  do {                                                                         \
    fprintf(stderr, "runtime error: ");                                        \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
    exit(1);                                                                   \
  } while (0)

static inline bool check_equality(Object *left, Object *right) {
#ifdef NAN_BOXING
  if (IS_NUM(*left) && IS_NUM(*right)) {
    return AS_NUM(*left) == AS_NUM(*right);
  }
  return *left == *right;
#else

  /* Return false if the objects are of different type. */
  if (left->type != right->type) {
    return false;
  }

  switch (left->type) {
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

static inline uint32_t adjust_idx(VM *vm, uint32_t idx) {
  /* 'idx' is adjusted to be relative to the current fra-
   * me pointer, if there are any. If not, it is returned
   * back, as is. */
  if (vm->fp_count > 0) {
    BytecodePtr fp = vm->fp_stack[vm->fp_count - 1];
    return fp.location + idx;
  } else {
    return idx;
  }
}

static inline char *concatenate_strings(char *a, char *b) {
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
static inline void handle_op_print(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_add(VM *vm, Bytecode *code, uint8_t **ip) {
  BINARY_OP(+, NUM_VAL);
}

/* OP_SUB pops two objects off the stack, subs them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_sub(VM *vm, Bytecode *code, uint8_t **ip) {
  BINARY_OP(-, NUM_VAL);
}

/* OP_MUL pops two objects off the stack, muls them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_mul(VM *vm, Bytecode *code, uint8_t **ip) {
  BINARY_OP(*, NUM_VAL);
}

/* OP_DIV pops two objects off the stack, divs them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_div(VM *vm, Bytecode *code, uint8_t **ip) {
  BINARY_OP(/, NUM_VAL);
}

/* OP_MOD pops two objects off the stack, mods them, and
 * pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_mod(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_bitand(VM *vm, Bytecode *code, uint8_t **ip) {
  BITWISE_OP(&);
}

/* OP_BITOR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise OR operati-
 * on on them, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_bitor(VM *vm, Bytecode *code, uint8_t **ip) {
  BITWISE_OP(|);
}

/* OP_BITXOR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise XOR operati-
 * on on them, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_bitxor(VM *vm, Bytecode *code, uint8_t **ip) {
  BITWISE_OP(^);
}

/* OP_BITNOT pops an object off the stack, clamps it to
 * to [0, UINT64_MAX], performs the bitwise NOT operat-
 * ion on it, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the object is a
 * number because this handler does not do a runtime type
 * check. */
static inline void handle_op_bitnot(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_bitshl(VM *vm, Bytecode *code, uint8_t **ip) {
  BITWISE_OP(<<);
}

/* OP_BITSHR pops two objects off the stack, clamps them
 * to [0, UINT64_MAX], performs the bitwise SHR operati-
 * on on them, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the two objec-
 * ts are numbers, because this handler does not do run-
 * time type checks. */
static inline void handle_op_bitshr(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_eq(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_gt(VM *vm, Bytecode *code, uint8_t **ip) {
  BINARY_OP(>, BOOL_VAL);
}

/* OP_LT pops two objects off the stack, compares them us-
 * ing the LT operation, and pushes the result back on the
 * stack.
 *
 * SAFETY: It is up to the user to ensure the two objects
 * are numbers, because this handler does not do runtime
 * type checks. */
static inline void handle_op_lt(VM *vm, Bytecode *code, uint8_t **ip) {
  BINARY_OP(<, BOOL_VAL);
}

/* OP_NOT pops an object off the stack, performs the
 * logical NOT operation on it by inverting its bool
 * value, and pushes the result back on the stack.
 *
 * SAFETY: It is up to the user to ensure the object
 * is a bool, because this handler does not do a ru-
 * ntime type check. */
static inline void handle_op_not(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_neg(VM *vm, Bytecode *code, uint8_t **ip) {
  Object original = pop(vm);
  Object negated = NUM_VAL(-AS_NUM(original));
  push(vm, negated);
}

/* OP_TRUE pushes a bool object ('true') on the stack. */
static inline void handle_op_true(VM *vm, Bytecode *code, uint8_t **ip) {
  push(vm, BOOL_VAL(true));
}

/* OP_NULL pushes a null object on the stack. */
static inline void handle_op_null(VM *vm, Bytecode *code, uint8_t **ip) {
  push(vm, NULL_VAL);
}

/* OP_CONST reads a 4-byte index of the constant in the
 * chunk's cp, constructs an object with that value and
 * pushes it on the stack. */
static inline void handle_op_const(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t idx = READ_UINT32();
  Object obj = NUM_VAL(code->cp.data[idx]);
  push(vm, obj);
}

/* OP_STR reads a 4-byte index of the string in the ch-
 * unk's sp, constructs a string object with that value
 * and pushes it on the stack.
 *
 * REFCOUNTING: Since Strings are refcounted, the newly
 * constructed object has a refcount=1. */
static inline void handle_op_str(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t idx = READ_UINT32();

  String s = {.refcount = 1, .value = own_string(code->sp.data[idx])};

  push(vm, STRING_VAL(ALLOC(s)));
}

/* OP_JZ reads a signed 2-byte offset (that could be ne-
 * gative), pops an object off the stack, and increments
 * the instruction pointer by the offset, if and only if
 * the popped object was 'false'. */
static inline void handle_op_jz(VM *vm, Bytecode *code, uint8_t **ip) {
  int16_t offset = READ_INT16();
  Object obj = pop(vm);
  if (!AS_BOOL(obj)) {
    *ip += offset;
  }
}

/* OP_JMP reads a signed 2-byte offset (that could be ne-
 * gative), and increments the instruction pointer by the
 * offset. Unlike OP_JZ, which is a conditional jump, the
 * OP_JMP instruction takes the jump unconditionally. */
static inline void handle_op_jmp(VM *vm, Bytecode *code, uint8_t **ip) {
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
 * */
static inline void handle_op_set_global(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t name_idx = READ_UINT32();
  Object obj = pop(vm);
  table_insert(&vm->globals, code->sp.data[name_idx], obj);
}

/* OP_GET_GLOBAL reads a 4-byte index of the variable name
 * in the chunk's sp, looks up an object with that name in
 * the vm's globals table, and pushes it on the stack.
 *
 * REFCOUNTING: Since the object will be present in yet an-
 * other location, the refcount must be incremented. */
static inline void handle_op_get_global(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t name_idx = READ_UINT32();
  Object *obj = table_get_unchecked(&vm->globals, code->sp.data[name_idx]);
  push(vm, *obj);
  objincref(obj);
}

/* OP_GET_GLOBAL_PTR reads a 4-byte index of the variable
 * name in the chunk's sp, looks up the object under that
 * name in in the vm's globals table, and pushes its add-
 * ress on the stack. */
static inline void handle_op_get_global_ptr(VM *vm, Bytecode *code,
                                            uint8_t **ip) {
  uint32_t name_idx = READ_UINT32();
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
static inline void handle_op_deepset(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_derefset(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_deepget(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_deepget_ptr(VM *vm, Bytecode *code, uint8_t **ip) {
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
 * SAFETY: the handler will try to ensure that the accessed
 * property is defined on the object being modified. */
static inline void handle_op_setattr(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t property_name_idx = READ_UINT32();

  Object value = pop(vm);
  Object obj = pop(vm);

  StructBlueprint *sb =
      table_get_unchecked(vm->blueprints, AS_STRUCT(obj)->name);

  int *idx = table_get(sb->property_indexes, code->sp.data[property_name_idx]);
  if (!idx) {
    RUNTIME_ERROR("struct '%s' does not have property '%s'",
                  AS_STRUCT(obj)->name, code->sp.data[property_name_idx]);
  }

  AS_STRUCT(obj)->properties[*idx] = value;

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
static inline void handle_op_getattr(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t property_name_idx = READ_UINT32();
  Object obj = pop(vm);

  StructBlueprint *sb =
      table_get_unchecked(vm->blueprints, AS_STRUCT(obj)->name);
  int *idx = table_get(sb->property_indexes, code->sp.data[property_name_idx]);
  if (!idx) {
    RUNTIME_ERROR("struct '%s' does not have property '%s'",
                  AS_STRUCT(obj)->name, code->sp.data[property_name_idx]);
  }

  Object property = AS_STRUCT(obj)->properties[*idx];

  push(vm, property);
  objincref(&property);
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
static inline void handle_op_getattr_ptr(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t property_name_idx = READ_UINT32();
  Object object = pop(vm);

  StructBlueprint *sb =
      table_get_unchecked(vm->blueprints, AS_STRUCT(object)->name);
  int *idx = table_get(sb->property_indexes, code->sp.data[property_name_idx]);
  if (!idx) {
    RUNTIME_ERROR("struct '%s' does not have property '%s'",
                  AS_STRUCT(object)->name, code->sp.data[property_name_idx]);
  }

  Object *property = &AS_STRUCT(object)->properties[*idx];
  push(vm, PTR_VAL(property));

  objdecref(&object);
}

/* OP_STRUCT reads a 4-byte index of the struct name in the
 * sp, constructs a struct object with that name and refco-
 * unt set to 1 (while making sure to initialize the prope-
 * rties table properly), and pushes it on the stack. */
static inline void handle_op_struct(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t structname = READ_UINT32();

  StructBlueprint *sb = table_get(vm->blueprints, code->sp.data[structname]);
  if (!sb) {
    RUNTIME_ERROR("struct '%s' is not defined", code->sp.data[structname]);
  }

  Struct s = {.name = code->sp.data[structname],
              .propcount = sb->property_indexes->count,
              .refcount = 1,
              .properties =
                  malloc(sizeof(Object) * sb->property_indexes->count)};

  for (size_t i = 0; i < s.propcount; i++) {
    s.properties[i] = NULL_VAL;
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
static inline void handle_op_struct_blueprint(VM *vm, Bytecode *code,
                                              uint8_t **ip) {
  uint32_t name_idx = READ_UINT32();
  uint32_t propcount = READ_UINT32();

  DynArray_char_ptr properties = {0};
  DynArray_uint32_t prop_indexes = {0};
  for (size_t i = 0; i < propcount; i++) {
    dynarray_insert(&properties, code->sp.data[READ_UINT32()]);
    dynarray_insert(&prop_indexes, READ_UINT32());
  }

  StructBlueprint sb = {.name = code->sp.data[name_idx],
                        .property_indexes = malloc(sizeof(Table_int)),
                        .methods = malloc(sizeof(Table_Function))};

  memset(sb.property_indexes, 0, sizeof(Table_int));
  memset(sb.methods, 0, sizeof(Table_Function));

  for (size_t i = 0; i < properties.count; i++) {
    table_insert(sb.property_indexes, properties.data[i], prop_indexes.data[i]);
  }

  table_insert(vm->blueprints, code->sp.data[name_idx], sb);

  dynarray_free(&properties);
  dynarray_free(&prop_indexes);
}

/* OP_CALL reads a 4-byte number uses it to construct a BytecodePtr
 * object and push it on the frame pointer stack.
 *
 * The address the BytecodePtr points to is the one of the next in-
 * struction that comes after the jump following the opcode and its
 * 4-byte operand.
 *
 * The location is where the index of the position where the frame starts. */
static inline void handle_op_call(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t argcount = READ_UINT32();
  BytecodePtr ip_obj = {.addr = *(ip) + 3, .location = vm->tos - argcount};
  vm->fp_stack[vm->fp_count++] = ip_obj;
}

static inline void handle_op_call_method(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t method_name_idx = READ_UINT32();

  /* look up the method argument count in the object's blueprint */
  Object object = pop(vm);
  StructBlueprint *sb =
      table_get_unchecked(vm->blueprints, AS_STRUCT(object)->name);

  Function *method = table_get(sb->methods, code->sp.data[method_name_idx]);

  int argcount = method->paramcount - 1;

  BytecodePtr ip_obj = {.addr = *ip, .location = vm->tos - argcount};
  vm->fp_stack[vm->fp_count++] = ip_obj;

  push(vm, object);

  *ip = &code->code.data[method->location - 1];
}

static inline void handle_op_impl(VM *vm, Bytecode *code, uint8_t **ip) {
  uint32_t blueprint_name_idx = READ_UINT32();
  uint32_t method_count = READ_UINT32();

  StructBlueprint *sb =
      table_get(vm->blueprints, code->sp.data[blueprint_name_idx]);
  if (!sb) {
    RUNTIME_ERROR("struct '%s' is not defined",
                  code->sp.data[blueprint_name_idx]);
  }

  for (size_t i = 0; i < method_count; i++) {
    uint32_t method_name_idx = READ_UINT32();
    uint32_t paramcount = READ_UINT32();
    uint32_t location = READ_UINT32();

    Function method = {
        .location = location,
        .paramcount = paramcount,
        .name = code->sp.data[method_name_idx],
    };

    table_insert(sb->methods, code->sp.data[method_name_idx], method);
  }
}

/* OP_RET pops a BytecodePtr off the frame pointer stack
 * and sets the instruction pointer to point to the add-
 * ress contained in the BytecodePtr. */
static inline void handle_op_ret(VM *vm, Bytecode *code, uint8_t **ip) {
  BytecodePtr retaddr = vm->fp_stack[--vm->fp_count];
  *ip = retaddr.addr;
}

/* OP_POP pops an object off the stack.
 *
 * REFCOUNTING: Since the popped object might be refcounted,
 * its refcount must be decremented. */
static inline void handle_op_pop(VM *vm, Bytecode *code, uint8_t **ip) {
  Object obj = pop(vm);
  objdecref(&obj);
}

/* OP_DEREF pops an object off the stack, dereferences it
 * and pushes it back on the stack.
 *
 * REFCOUNTING: Since the object will now be present in one
 * more another location, its refcount must be incremented. */
static inline void handle_op_deref(VM *vm, Bytecode *code, uint8_t **ip) {
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
static inline void handle_op_strcat(VM *vm, Bytecode *code, uint8_t **ip) {
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
    RUNTIME_ERROR(
        "'++' operator used on objects of unsupported types: %s and %s",
        get_object_type(&a), get_object_type(&b));
  }
}

#ifdef venom_debug_vm
static inline const char *print_current_instruction(uint8_t opcode) {
  switch (opcode) {
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
  case OP_IMPL:
    return "OP_IMPL";
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
  default:
    assert(0);
  }
}
#endif

void run(VM *vm, Bytecode *code) {
#ifdef venom_debug_disassembler
  disassemble(code);
#endif

  for (uint8_t *ip = code->code.data;
       ip < &code->code.data[code->code.count]; /* ip < addr of just beyond
                                                     the last instruction */
       ip++) {

#ifdef venom_debug_vm
    printf("%s\n", print_current_instruction(*ip));
#endif

    switch (*ip) {
    case OP_PRINT: {
      handle_op_print(vm, code, &ip);
      break;
    }
    case OP_ADD: {
      handle_op_add(vm, code, &ip);
      break;
    }
    case OP_SUB: {
      handle_op_sub(vm, code, &ip);
      break;
    }
    case OP_MUL: {
      handle_op_mul(vm, code, &ip);
      break;
    }
    case OP_DIV: {
      handle_op_div(vm, code, &ip);
      break;
    }
    case OP_MOD: {
      handle_op_mod(vm, code, &ip);
      break;
    }
    case OP_EQ: {
      handle_op_eq(vm, code, &ip);
      break;
    }
    case OP_GT: {
      handle_op_gt(vm, code, &ip);
      break;
    }
    case OP_LT: {
      handle_op_lt(vm, code, &ip);
      break;
    }
    case OP_NOT: {
      handle_op_not(vm, code, &ip);
      break;
    }
    case OP_NEG: {
      handle_op_neg(vm, code, &ip);
      break;
    }
    case OP_TRUE: {
      handle_op_true(vm, code, &ip);
      break;
    }
    case OP_NULL: {
      handle_op_null(vm, code, &ip);
      break;
    }
    case OP_CONST: {
      handle_op_const(vm, code, &ip);
      break;
    }
    case OP_STR: {
      handle_op_str(vm, code, &ip);
      break;
    }
    case OP_JZ: {
      handle_op_jz(vm, code, &ip);
      break;
    }
    case OP_JMP: {
      handle_op_jmp(vm, code, &ip);
      break;
    }
    case OP_BITAND: {
      handle_op_bitand(vm, code, &ip);
      break;
    }
    case OP_BITOR: {
      handle_op_bitor(vm, code, &ip);
      break;
    }
    case OP_BITXOR: {
      handle_op_bitxor(vm, code, &ip);
      break;
    }
    case OP_BITNOT: {
      handle_op_bitnot(vm, code, &ip);
      break;
    }
    case OP_BITSHL: {
      handle_op_bitshl(vm, code, &ip);
      break;
    }
    case OP_BITSHR: {
      handle_op_bitshr(vm, code, &ip);
      break;
    }
    case OP_SET_GLOBAL: {
      handle_op_set_global(vm, code, &ip);
      break;
    }
    case OP_GET_GLOBAL: {
      handle_op_get_global(vm, code, &ip);
      break;
    }
    case OP_GET_GLOBAL_PTR: {
      handle_op_get_global_ptr(vm, code, &ip);
      break;
    }
    case OP_DEEPSET: {
      handle_op_deepset(vm, code, &ip);
      break;
    }
    case OP_DEEPGET: {
      handle_op_deepget(vm, code, &ip);
      break;
    }
    case OP_DEEPGET_PTR: {
      handle_op_deepget_ptr(vm, code, &ip);
      break;
    }
    case OP_SETATTR: {
      handle_op_setattr(vm, code, &ip);
      break;
    }
    case OP_GETATTR: {
      handle_op_getattr(vm, code, &ip);
      break;
    }
    case OP_GETATTR_PTR: {
      handle_op_getattr_ptr(vm, code, &ip);
      break;
    }
    case OP_STRUCT: {
      handle_op_struct(vm, code, &ip);
      break;
    }
    case OP_STRUCT_BLUEPRINT: {
      handle_op_struct_blueprint(vm, code, &ip);
      break;
    }
    case OP_IMPL: {
      handle_op_impl(vm, code, &ip);
      break;
    }
    case OP_CALL: {
      handle_op_call(vm, code, &ip);
      break;
    }
    case OP_CALL_METHOD: {
      handle_op_call_method(vm, code, &ip);
      break;
    }
    case OP_RET: {
      handle_op_ret(vm, code, &ip);
      break;
    }
    case OP_POP: {
      handle_op_pop(vm, code, &ip);
      break;
    }
    case OP_DEREF: {
      handle_op_deref(vm, code, &ip);
      break;
    }
    case OP_DEREFSET: {
      handle_op_derefset(vm, code, &ip);
      break;
    }
    case OP_STRCAT: {
      handle_op_strcat(vm, code, &ip);
      break;
    }
    default:
      break;
    }

#ifdef venom_debug_vm
    PRINT_STACK();
#endif
  }

  assert(vm->tos == 0);
}
