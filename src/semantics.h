#ifndef VENOM_SEMANTICS_H
#define VENOM_SEMANTICS_H

#include "parser.h"

DynArray_Stmt loop_label_program(DynArray_Stmt stmts, char *current);
void loop_label_stmt(Stmt *stmt, char *current);

#endif