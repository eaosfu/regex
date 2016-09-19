#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H
#include "nfa.h"
#include "token.h"
#include "stack.h"
#include "scanner.h"


typedef struct IntervalRecord {
  NFA * node;
  unsigned int min;
  unsigned int max;
} IntervalRecord;


typedef struct Parser {
  int err_msg_available;
  char err_msg[50];
  Token lookahead;
  Stack * symbol_stack;
  Stack * interval_stack;
  Scanner * scanner;
} Parser;


Parser * init_parser(FILE *);
void parser_free(Parser *);
void parse_regex(Parser *);

#endif
