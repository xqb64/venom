#ifndef venom_disassembler_h
#define venom_disassembler_h

#include "compiler.h"

void disassemble(Bytecode *code);

typedef struct {
  char *opcode;
} DisassembleHandler;

extern DisassembleHandler disassemble_handler[];

#endif
