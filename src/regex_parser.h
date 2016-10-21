#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H
#include "nfa.h"
#include "token.h"
#include "stack.h"
#include "scanner.h"


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
  NFACtrl * nfa_ctrl;
  ctrl_flags * ctrl_flags;
  int paren_count;
  int capture_group_count;
  NFA * capture_group_list[9];
} Parser;


Parser * init_parser(Scanner *, ctrl_flags *);
void parser_free(Parser *);
void parse_regex(Parser *);

#endif
