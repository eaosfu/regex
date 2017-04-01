#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H
#include "nfa.h"
#include "token.h"
#include "stack.h"
#include "scanner.h"
#include "mpat.h" // should be moved the the 'compile' module
#include "cgrps.h"


typedef struct CaptureGrpRecord {
  int next_id;
  char * end;
} CaptureGrpRecord;


typedef struct Parser {
  Token lookahead;
  Scanner    * scanner;
  Stack      * symbol_stack;
  Stack      * branch_stack;
  NFA        * prev_interval;
  NFA        * prev_interval_head;
  NFACtrl    * nfa_ctrl;
  MPatObj    * mpat_obj;
  ctrl_flags * ctrl_flags;

  // Count open parens
  int paren_count;

  // count the number of branches in the current nested sub-expression
  int subtree_branch_count;

  // Capture-Group Stuff
  int cgrp_count;
  int current_cgrp;
  int in_new_cgrp;
  int paren_idx;
  //int paren_stack[CGRP_MAX];
  int * paren_stack;

  // Alternation Stuff
  int in_alternation;
  int tree_count;
  int branch;
  int lowest_id_on_branch;

  // Interval Stuff
  int interval_count;

  int total_nfa_ids;

  CaptureGrpRecord capgrp_record[CGRP_MAX];
  BIT_MAP_TYPE cgrp_map[CGRP_MAP_SIZE];
} Parser;


Parser * init_parser(Scanner *, ctrl_flags *);
void parser_free(Parser *);
int parse_regex(Parser *);

#endif
