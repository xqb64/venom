#ifndef venom_semantics_h
#define venom_semantics_h

#include "parser.h"

typedef struct
{
    bool is_ok;
    char *msg;
} LoopLabelResult;

LoopLabelResult loop_label_program(DynArray_Stmt *stmts, char *current);
LoopLabelResult loop_label_stmt(Stmt *stmt, char *current);

#endif
