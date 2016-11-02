#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H
#include "nfa.h"
#include "token.h"
#include "stack.h"
#include "scanner.h"

#define CAPTURE_GROUP_MAX 9


typedef struct CaptureGroupRercord {
  int count;         // track total capture grousp seen
  int current_idx;   // track the num of the current capture group
  int next_cgrp_idx;
  // table with referenced capture groups marked as 1
  unsigned int is_referenced[CAPTURE_GROUP_MAX]; // convert this to a bit-field
  // stack of open capture groups
  unsigned int open_cgrp[CAPTURE_GROUP_MAX];
} CaptureGroupRecord;


typedef struct IntervalRecord {
  NFA * node;
  unsigned int min_rep;
  unsigned int max_rep;
  unsigned int count;
} IntervalRecord;


typedef struct Parser {
  Token lookahead;
  Stack   * symbol_stack;
  Stack   * interval_stack;
  Scanner * scanner;
  CaptureGroupRecord cg_record;
  NFACtrl * nfa_ctrl;
  ctrl_flags * ctrl_flags;
  int paren_count;
  int in_alternation;
  int branch_id;
  int ignore_missing_paren;
} Parser;


Parser * init_parser(Scanner *, ctrl_flags *);
void parser_free(Parser *);
void parse_regex(Parser *);

#endif
