#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H
#include "nfa.h"
#include "token.h"
#include "stack.h"
#include "scanner.h"
#include "mpat.h" // should be moved the the 'compile' module
#include "cgrps.h"

#include "mpat.h"
#include "boyer_moore.h"


typedef struct CaptureGrpRecord {
  int next_id;
  char * end;
} CaptureGrpRecord;


typedef struct Parser {
  Scanner    * scanner;
  Stack      * symbol_stack;
  Token        lookahead;
  NFACtrl    * nfa_ctrl;
  ctrl_flags * ctrl_flags;
  Stack      * branch_stack;
  List       * synth_patterns;
  int        * paren_stack;
  NFA        * prev_interval;
  NFA        * prev_interval_head;
  const char * program_name;


  // Count open parens
  int paren_count;

  // count the number of branches in the current nested sub-expression
  int subtree_branch_count;

  // Capture-Group Stuff
  int cgrp_count;
  int current_cgrp;
  int in_new_cgrp;
  int paren_idx;

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


Parser * init_parser(const char *, Scanner *, ctrl_flags *);
void parser_free(Parser *);
int parse_regex(Parser *);

#endif
