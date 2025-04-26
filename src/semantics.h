#ifndef venom_semantics_h
#define venom_semantics_h

#include "parser.h"

DynArray_Stmt loop_label_program(DynArray_Stmt stmts, char *current);
void loop_label_stmt(Stmt *stmt, char *current);

#endif
