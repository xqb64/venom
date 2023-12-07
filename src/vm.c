#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
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
    int64_t truncated_a = (int64_t)AS_NUM(a);                                  \
    int64_t truncated_b = (int64_t)AS_NUM(b);                                  \
                                                                               \
    int32_t reduced_a = (int32_t)(truncated_a % (int64_t)(1LL << 32));         \
    int32_t reduced_b = (int32_t)(truncated_b % (int64_t)(1LL << 32));         \
                                                                               \
    Object obj = NUM_VAL(reduced_a op reduced_b);                              \
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

#define RUNTIME_ERROR(...)                                                     \
  do {                                                                         \
    fprintf(stderr, "runtime error: ");                                        \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
    return 1;                                                                  \
  } while (0)

static inline bool check_equality(Object *left, Object *right) {
#ifdef NAN_BOXING
  if (IS_NUM(*left) && IS_NUM(*right)) {
    return AS_NUM(*left) == AS_NUM(*right);
  }
  if (IS_STRUCT(*left) && IS_STRUCT(*right)) {
    Struct *a = AS_STRUCT(*left);
    Struct *b = AS_STRUCT(*right);

    /* Return false if the structs are of different types. */
    if (strcmp(a->name, b->name) != 0) {
      return false;
    }

    /* If they have the same type, for each non-NULL
     * property in struct 'a', run the func recursi-
     * vely comparing that property with the corres-
     * ponding property in struct 'b'. */
    for (size_t i = 0; i < a->propcount; i++) {
      if (!check_equality(&a->properties[i], &b->properties[i])) {
        return false;
      }
    }
    /* Comparing the properties didn't return false,
     * which means that the two structs are equal. */
    return true;
  }
  return *left == *right;
#else

  /* Return false if the objects are of different type. */
  if (left->type != right->type) {
    return false;
  }

  switch (left->type) {
  case OBJ_OBJ: {
    switch (AS_OBJ(*left)->type) {
    case OBJ_STRING: {
      return strcmp(AS_STR(*left)->value, AS_STR(*right)->value) == 0;
    }
    case OBJ_STRUCT: {
      Struct *a = AS_STRUCT(*left);
      Struct *b = AS_STRUCT(*right);

      /* Return false if the structs are of different types. */
      if (strcmp(a->name, b->name) != 0) {
        return false;
      }

      /* If they have the same type, for each non-NULL
       * property in struct 'a', run the func recursi-
       * vely comparing that property with the corres-
       * ponding property in struct 'b'. */
      for (size_t i = 0; i < a->propcount; i++) {
        if (!check_equality(&a->properties[i], &b->properties[i])) {
          return false;
        }
      }
      /* Comparing the properties didn't return false,
       * which means that the two structs are equal. */
      return true;
    }
    default:
      assert(0);
    }
    break;
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
  /* 'idx' is adjusted to be relative to the
   * current frame pointer ('adjustment' ta-
   * kes care of the case where there are no
   * frame pointers on the stack). */
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

static inline int handle_op_print(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_PRINT pops an object off the stack and prints it.
   * Since the popped object might be refcounted, the re-
   * ference count must be decremented. */
  Object object = pop(vm);
#ifdef venom_debug_vm
  printf("dbg print :: ");
#endif
  print_object(&object);
  printf("\n");
  objdecref(&object);
  return 0;
}

static inline int handle_op_add(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BINARY_OP(+, NUM_VAL);
  return 0;
}

static inline int handle_op_sub(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BINARY_OP(-, NUM_VAL);
  return 0;
}

static inline int handle_op_mul(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BINARY_OP(*, NUM_VAL);
  return 0;
}

static inline int handle_op_div(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BINARY_OP(/, NUM_VAL);
  return 0;
}

static inline int handle_op_mod(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  Object b = pop(vm);
  Object a = pop(vm);
  Object obj = NUM_VAL(fmod(AS_NUM(a), AS_NUM(b)));
  push(vm, obj);
  return 0;
}

static inline int handle_op_bitand(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BITWISE_OP(&);
  return 0;
}

static inline int handle_op_bitor(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BITWISE_OP(|);
  return 0;
}

static inline int handle_op_bitxor(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BITWISE_OP(^);
  return 0;
}

static inline int handle_op_bitnot(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  Object obj = pop(vm);
  double value = AS_NUM(obj);

  /* discard the fractional part */
  int64_t truncated = (int64_t)value;

  /* reduce modulo 2^32 to fit into 32-bit int */
  int32_t reduced = (int32_t)(truncated % (int64_t)(1LL << 32));

  /* apply bitwise not */
  int32_t inverted = ~reduced;

  /* convert back to double */
  push(vm, NUM_VAL(inverted));
  return 0;
}

static inline int handle_op_bitshl(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BITWISE_OP(<<);
  return 0;
}

static inline int handle_op_bitshr(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BITWISE_OP(>>);
  return 0;
}

static inline int handle_op_eq(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  Object b = pop(vm);
  Object a = pop(vm);
  /* Since the two objects might be refcounted,
   * the reference count must be decremented. */
  objdecref(&a);
  objdecref(&b);
  push(vm, BOOL_VAL(check_equality(&a, &b)));
  return 0;
}

static inline int handle_op_gt(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BINARY_OP(>, BOOL_VAL);
  return 0;
}

static inline int handle_op_lt(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  BINARY_OP(<, BOOL_VAL);
  return 0;
}

static inline int handle_op_not(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_NOT pops an object off the stack and pushes
   * its inverse back on the stack. The popped obj-
   * ect must be a boolean.  */
  Object obj = pop(vm);
  push(vm, BOOL_VAL(AS_BOOL(obj) ^ 1));
  return 0;
}

static inline int handle_op_neg(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_NEG pops an object off the stack, negates
   * it and pushes the negative back on the stack. */
  Object original = pop(vm);
  Object negated = NUM_VAL(-AS_NUM(original));
  push(vm, negated);
  return 0;
}

static inline int handle_op_true(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_TRUE pushes a boolean object ('true') on the stack. */
  push(vm, BOOL_VAL(true));
  return 0;
}

static inline int handle_op_null(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_NULL pushes a null object on the stack. */
  push(vm, NULL_VAL);
  return 0;
}

static inline int handle_op_const(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_CONST reads a 4-byte index of the constant in the
   * chunk's cp, constructs an object with that value and
   * pushes it on the stack. Since constants are not ref-
   * counted, incrementing the refcount is not needed. */
  uint32_t idx = READ_UINT32();
  Object obj = NUM_VAL(chunk->cp.data[idx]);
  push(vm, obj);
  return 0;
}

static inline int handle_op_str(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_STR reads a 4-byte index of the string in the ch-
   * unk's sp, constructs a string object with that value
   * and pushes it on the stack. */
  uint32_t idx = READ_UINT32();

  String *s = malloc(sizeof(String));
  s->refcount = 1;
  s->value = own_string(chunk->sp.data[idx]);

  Obj obj = {.type = OBJ_STRING, .as.str = s};
  push(vm, OBJ_VAL(ALLOC(obj)));
  return 0;
}

static inline int handle_op_jz(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_JZ reads a signed 2-byte offset (that could be ne-
   * gative), pops an object off the stack, and increments
   * the instruction pointer by the offset, if and only if
   * the popped object was 'false'. */
  int16_t offset = READ_INT16();
  Object obj = pop(vm);
  if (!AS_BOOL(obj)) {
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

static inline int handle_op_set_global(VM *vm, BytecodeChunk *chunk,
                                       uint8_t **ip) {
  /* OP_SET_GLOBAL reads a 4-byte index of the variable name
   * in the chunk's sp, pops an object off the stack and in-
   * serts it into the vm's globals table under that name. */
  uint32_t name_idx = READ_UINT32();
  Object obj = pop(vm);
  table_insert(&vm->globals, chunk->sp.data[name_idx], obj);
  return 0;
}

static inline int handle_op_get_global(VM *vm, BytecodeChunk *chunk,
                                       uint8_t **ip) {
  /* OP_GET_GLOBAL reads a 4-byte index of the variable name
   * in the chunk's sp, looks up an object with that name in
   * the vm's globals table, and pushes it on the stack. Si-
   * nce the object will be present in yet another location,
   * the refcount must be incremented. */
  uint32_t name_idx = READ_UINT32();
  Object *obj = table_get_unchecked(&vm->globals, chunk->sp.data[name_idx]);
  push(vm, *obj);
  objincref(obj);
  return 0;
}

static inline int handle_op_get_global_ptr(VM *vm, BytecodeChunk *chunk,
                                           uint8_t **ip) {
  /* OP_GET_GLOBAL_PTR reads a 4-byte index of the variable
   * name in the chunk's sp, looks up the object under that
   * name in in the vm's globals table, and pushes its add-
   * ress on the stack. */
  uint32_t name_idx = READ_UINT32();
  Object *object = table_get_unchecked(&vm->globals, chunk->sp.data[name_idx]);
  Pointer ptr = {.refcount = 1, .ptr = object};
  Obj obj = {.type = OBJ_PTR, .as.ptr = ALLOC(ptr)};
  push(vm, OBJ_VAL(ALLOC(obj)));
  return 0;
}

static inline int handle_op_deepset(VM *vm, BytecodeChunk *chunk,
                                    uint8_t **ip) {
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
  objdecref(&vm->stack[adjusted_idx]);
  vm->stack[adjusted_idx] = obj;
  return 0;
}

static inline int handle_op_derefset(VM *vm, BytecodeChunk *chunk,
                                     uint8_t **ip) {
  /* OP_DEREFSET expects two things to already be on the
   * stack:
   * - the value that is being assigned
   * - the assignee (a pointer)
   * It then pops those two objects off the stack, and
   * essentially changes what the pointer points to.
   */
  Object item = pop(vm);
  Object *ptr = AS_PTR(pop(vm));

  *ptr = item;

  objdecref(ptr);

  return 0;
}

static inline int handle_op_deepget(VM *vm, BytecodeChunk *chunk,
                                    uint8_t **ip) {
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
  objincref(&obj);
  return 0;
}

static inline int handle_op_deepget_ptr(VM *vm, BytecodeChunk *chunk,
                                        uint8_t **ip) {
  /* OP_DEEPGET_PTR reads a 4-byte index (1-based) of the
   * object being accessed, which is adjusted and used to
   * access the object in that position and push its add-
   * ress on the stack. */
  uint32_t idx = READ_UINT32();
  uint32_t adjusted_idx = adjust_idx(vm, idx);
  Object *object = &vm->stack[adjusted_idx];
  Pointer ptr = {.refcount = 1, .ptr = object};
  Obj obj = {.type = OBJ_PTR, .as.ptr = ALLOC(ptr)};
  push(vm, OBJ_VAL(ALLOC(obj)));
  return 0;
}

static inline int handle_op_setattr(VM *vm, BytecodeChunk *chunk,
                                    uint8_t **ip) {
  /* OP_SETATTR reads a 4-byte index of the property name in
   * the chunk's sp, pops two objects off the stack (a value
   * of the property, and the object being modified) and in-
   * serts the value into the object's properties Table. Th-
   * en it pushes the modified object back on the stack. */
  uint32_t property_name_idx = READ_UINT32();
  Object value = pop(vm);
  Object obj = pop(vm);
  StructBlueprint *sb =
      table_get_unchecked(vm->blueprints, AS_STRUCT(obj)->name);
  int *idx = table_get(sb->property_indexes, chunk->sp.data[property_name_idx]);
  if (!idx) {
    RUNTIME_ERROR("struct '%s' does not have property '%s'",
                  AS_STRUCT(obj)->name, chunk->sp.data[property_name_idx]);
  }
  AS_STRUCT(obj)->properties[*idx] = value;
  push(vm, obj);
  return 0;
}

static inline int handle_op_getattr(VM *vm, BytecodeChunk *chunk,
                                    uint8_t **ip) {
  /* OP_GETATTR reads a 4-byte index of the property name in
   * the sp. Then, it pops an object off the stack, and loo-
   * ks up the property with that name in its properties Ta-
   * ble. If the property is found, it will be pushed on the
   * stack. Otherwise, a runtime error is raised. */
  uint32_t property_name_idx = READ_UINT32();
  Object obj = pop(vm);

  StructBlueprint *sb =
      table_get_unchecked(vm->blueprints, AS_STRUCT(obj)->name);
  int *idx = table_get(sb->property_indexes, chunk->sp.data[property_name_idx]);
  if (!idx) {
    RUNTIME_ERROR("struct '%s' does not have property '%s'",
                  AS_STRUCT(obj)->name, chunk->sp.data[property_name_idx]);
  }

  Object property = AS_STRUCT(obj)->properties[*idx];

  push(vm, property);
  objincref(&property);
  objdecref(&obj);
  return 0;
}

static inline int handle_op_getattr_ptr(VM *vm, BytecodeChunk *chunk,
                                        uint8_t **ip) {
  /* OP_GETATTR_PTR reads a 4-byte index of the property name
   * in the chunk's sp. Then, it pops an object off the stack
   * and looks up the property with that name in the object's
   * properties Table. If the property is found, a pointer to
   * it is pushed on the stack (conveniently, table_get() re-
   * turns a pointer). Otherwise, a runtime error is raised. */
  uint32_t property_name_idx = READ_UINT32();
  Object object = pop(vm);

  StructBlueprint *sb =
      table_get_unchecked(vm->blueprints, AS_STRUCT(object)->name);
  int *idx = table_get(sb->property_indexes, chunk->sp.data[property_name_idx]);
  if (!idx) {
    RUNTIME_ERROR("struct '%s' does not have property '%s'",
                  AS_STRUCT(object)->name, chunk->sp.data[property_name_idx]);
  }

  Object *property = &AS_STRUCT(object)->properties[*idx];
  Pointer ptr = {.refcount = 1, .ptr = property};
  Obj obj = {.type = OBJ_PTR, .as.ptr = ALLOC(ptr)};

  push(vm, OBJ_VAL(property));
  objdecref(&object);
  return 0;
}

static inline int handle_op_struct(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_STRUCT reads a 4-byte index of the struct name in the
   * sp, constructs a struct object with that name and refco-
   * unt set to 1 (while making sure to initialize the prope-
   * rties table properly), and pushes it on the stack. */
  uint32_t structname = READ_UINT32();

  StructBlueprint *sb = table_get(vm->blueprints, chunk->sp.data[structname]);
  if (!sb) {
    RUNTIME_ERROR("struct '%s' is not defined", chunk->sp.data[structname]);
  }

  Struct *s =
      malloc(sizeof(Struct) + sizeof(Object) * sb->property_indexes->count);

  s->name = chunk->sp.data[structname];
  s->propcount = sb->property_indexes->count;
  s->refcount = 1;
  s->properties = malloc(sizeof(Object) * s->propcount);

  for (size_t i = 0; i < s->propcount; i++) {
    s->properties[i] = NULL_VAL;
  }

  Obj obj = {.type = OBJ_STRUCT, .as.structobj = s};

  push(vm, OBJ_VAL(ALLOC(obj)));
  return 0;
}

static inline int handle_op_struct_blueprint(VM *vm, BytecodeChunk *chunk,
                                             uint8_t **ip) {
  /* OP_STRUCT_BLUEPRINT reads a 4-byte name index of the
   * struct name in the sp, then it reads a 4-byte prope-
   * rty count of the said struct (let's call this propc-
   * ount). Then, it loops 'propcount' times and for each
   * property, it reads the name index in the sp, and pr-
   * operty index (in the items array in the Table_int of
   * the StructBlueprint). Finally, it uses all this info
   * to construct a StructBlueprint object, initialize it
   * properly, and insert it into the vm's blueprints ta-
   * ble.
   * */
  uint32_t name_idx = READ_UINT32();
  uint32_t propcount = READ_UINT32();
  DynArray_char_ptr properties = {0};
  DynArray_uint32_t prop_indexes = {0};
  for (size_t i = 0; i < propcount; i++) {
    dynarray_insert(&properties, chunk->sp.data[READ_UINT32()]);
    dynarray_insert(&prop_indexes, READ_UINT32());
  }

  StructBlueprint sb = {.name = chunk->sp.data[name_idx],
                        .property_indexes = malloc(sizeof(Table_int))};

  memset(sb.property_indexes, 0, sizeof(Table_int));

  for (size_t i = 0; i < properties.count; i++) {
    table_insert(sb.property_indexes, properties.data[i], prop_indexes.data[i]);
  }

  table_insert(vm->blueprints, chunk->sp.data[name_idx], sb);
  dynarray_free(&properties);
  dynarray_free(&prop_indexes);
  return 0;
}

static inline int handle_op_call(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
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
  uint32_t argcount = READ_UINT32();
  BytecodePtr ip_obj = {.addr = *(ip) + 3, .location = vm->tos - argcount};
  vm->fp_stack[vm->fp_count++] = ip_obj;
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
  BytecodePtr retaddr = vm->fp_stack[--vm->fp_count];
  *ip = retaddr.addr;
  return 0;
}

static inline int handle_op_pop(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_POP pops an object off the stack. Since the popped
   * object might be refcounted, its refcount must be dec-
   * remented. */
  Object obj = pop(vm);
  objdecref(&obj);
  return 0;
}

static inline int handle_op_dup(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_DUP duplicates the top value on the stack. Since the
   * duplicated object is now present in one more place, its
   * refcount must be incremented. */
  Object obj = vm->stack[vm->tos - 1];
  push(vm, obj);
  objincref(&obj);
  return 0;
}

static inline int handle_op_deref(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_DEREF pops an object off the stack, dereferences it
   * and pushes it back on the stack. Since the object will
   * be present in one more another location, its reference
   * count must be incremented.*/
  Object obj = pop(vm);
  push(vm, *AS_PTR(obj));
  objdecref(&obj);
  objincref(AS_PTR(obj));

  return 0;
}

static inline int handle_op_strcat(VM *vm, BytecodeChunk *chunk, uint8_t **ip) {
  /* OP_STRCAT pops two objects off the stack, checks if they
   * are both Strings, and if so, concatenates them and pushes
   * the resulting string on the stack. Since Strings are ref-
   * counted objects, the refcount for the popped objects needs
   * to be decremented. The resulting string is initalized with
   * the refcount of 1. */
  Object b = pop(vm);
  Object a = pop(vm);
  if (IS_STRING(a) && IS_STRING(b)) {
    char *result = concatenate_strings(AS_STR(a)->value, AS_STR(b)->value);

    String *s = malloc(sizeof(String));
    s->refcount = 1;
    s->value = result;

    Obj obj = {.type = OBJ_STRING, .as.str = s};

    push(vm, OBJ_VAL(ALLOC(obj)));

    objdecref(&b);
    objdecref(&a);
  } else {
    RUNTIME_ERROR(
        "'++' operator used on objects of unsupported types: %s and %s",
        get_object_type(&a), get_object_type(&b));
  }
  return 0;
}

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
  case OP_CALL:
    return "OP_CALL";
  case OP_RET:
    return "OP_RET";
  case OP_POP:
    return "OP_POP";
  case OP_DUP:
    return "OP_DUP";
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

int run(VM *vm, BytecodeChunk *chunk) {
#ifdef venom_debug_disassembler
  disassemble(chunk);
#endif

  for (uint8_t *ip = chunk->code.data;
       ip < &chunk->code.data[chunk->code.count]; /* ip < addr of just beyond
                                                     the last instruction */
       ip++) {
#ifdef venom_debug_vm
    printf("%s\n", print_current_instruction(*ip));
#endif
    int status;
    switch (*ip) {
    case OP_PRINT: {
      status = handle_op_print(vm, chunk, &ip);
      break;
    }
    case OP_ADD: {
      status = handle_op_add(vm, chunk, &ip);
      break;
    }
    case OP_SUB: {
      status = handle_op_sub(vm, chunk, &ip);
      break;
    }
    case OP_MUL: {
      status = handle_op_mul(vm, chunk, &ip);
      break;
    }
    case OP_DIV: {
      status = handle_op_div(vm, chunk, &ip);
      break;
    }
    case OP_MOD: {
      status = handle_op_mod(vm, chunk, &ip);
      break;
    }
    case OP_EQ: {
      status = handle_op_eq(vm, chunk, &ip);
      break;
    }
    case OP_GT: {
      status = handle_op_gt(vm, chunk, &ip);
      break;
    }
    case OP_LT: {
      status = handle_op_lt(vm, chunk, &ip);
      break;
    }
    case OP_NOT: {
      status = handle_op_not(vm, chunk, &ip);
      break;
    }
    case OP_NEG: {
      status = handle_op_neg(vm, chunk, &ip);
      break;
    }
    case OP_TRUE: {
      status = handle_op_true(vm, chunk, &ip);
      break;
    }
    case OP_NULL: {
      status = handle_op_null(vm, chunk, &ip);
      break;
    }
    case OP_CONST: {
      status = handle_op_const(vm, chunk, &ip);
      break;
    }
    case OP_STR: {
      status = handle_op_str(vm, chunk, &ip);
      break;
    }
    case OP_JZ: {
      status = handle_op_jz(vm, chunk, &ip);
      break;
    }
    case OP_JMP: {
      status = handle_op_jmp(vm, chunk, &ip);
      break;
    }
    case OP_BITAND: {
      status = handle_op_bitand(vm, chunk, &ip);
      break;
    }
    case OP_BITOR: {
      status = handle_op_bitor(vm, chunk, &ip);
      break;
    }
    case OP_BITXOR: {
      status = handle_op_bitxor(vm, chunk, &ip);
      break;
    }
    case OP_BITNOT: {
      status = handle_op_bitnot(vm, chunk, &ip);
      break;
    }
    case OP_BITSHL: {
      status = handle_op_bitshl(vm, chunk, &ip);
      break;
    }
    case OP_BITSHR: {
      status = handle_op_bitshr(vm, chunk, &ip);
      break;
    }
    case OP_SET_GLOBAL: {
      status = handle_op_set_global(vm, chunk, &ip);
      break;
    }
    case OP_GET_GLOBAL: {
      status = handle_op_get_global(vm, chunk, &ip);
      break;
    }
    case OP_GET_GLOBAL_PTR: {
      status = handle_op_get_global_ptr(vm, chunk, &ip);
      break;
    }
    case OP_DEEPSET: {
      status = handle_op_deepset(vm, chunk, &ip);
      break;
    }
    case OP_DEEPGET: {
      status = handle_op_deepget(vm, chunk, &ip);
      break;
    }
    case OP_DEEPGET_PTR: {
      status = handle_op_deepget_ptr(vm, chunk, &ip);
      break;
    }
    case OP_SETATTR: {
      status = handle_op_setattr(vm, chunk, &ip);
      break;
    }
    case OP_GETATTR: {
      status = handle_op_getattr(vm, chunk, &ip);
      break;
    }
    case OP_GETATTR_PTR: {
      status = handle_op_getattr_ptr(vm, chunk, &ip);
      break;
    }
    case OP_STRUCT: {
      status = handle_op_struct(vm, chunk, &ip);
      break;
    }
    case OP_STRUCT_BLUEPRINT: {
      status = handle_op_struct_blueprint(vm, chunk, &ip);
      break;
    }
    case OP_CALL: {
      status = handle_op_call(vm, chunk, &ip);
      break;
    }
    case OP_RET: {
      status = handle_op_ret(vm, chunk, &ip);
      break;
    }
    case OP_POP: {
      status = handle_op_pop(vm, chunk, &ip);
      break;
    }
    case OP_DUP: {
      status = handle_op_dup(vm, chunk, &ip);
      break;
    }
    case OP_DEREF: {
      status = handle_op_deref(vm, chunk, &ip);
      break;
    }
    case OP_DEREFSET: {
      status = handle_op_derefset(vm, chunk, &ip);
      break;
    }
    case OP_STRCAT: {
      status = handle_op_strcat(vm, chunk, &ip);
      break;
    }
    default:
      break;
    }
    if (status != 0)
      return status;
#ifdef venom_debug_vm
    PRINT_STACK();
#endif
  }

  return 0;
}