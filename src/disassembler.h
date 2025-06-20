#ifndef venom_disassembler_h
#define venom_disassembler_h

#include <stdbool.h>

#include "compiler.h"

typedef struct {
  char *opcode;
} DisassembleHandler;

typedef struct {
  int errcode;
  bool is_ok;
  char *msg;
  double time;
} DisassembleResult;

DisassembleResult disassemble(Bytecode *code);

extern DisassembleHandler disassemble_handler[];

#endif
