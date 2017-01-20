#ifndef SCANNER_H_
#define SCANNER_H_

#include "misc.h"
#include "stack.h"
#include "token.h"

typedef struct Scanner {
  int line_no;
  long int line_len;
  unsigned long int buf_len;
  ctrl_flags * ctrl_flags;
  char * buffer;
  char * readhead;
  char * str_begin; 
  char * last_newline;
  int eol_symbol;
  Token * curtoken;
} Scanner;


Token * regex_scan(Scanner *);
Scanner * init_scanner(char *, unsigned int, unsigned int, ctrl_flags *);
void unput(Scanner *);
void free_scanner(Scanner *);
void restart_from(Scanner *, char *);
void reset_scanner(Scanner *);
int next_char(Scanner *);
char * get_scanner_readhead(Scanner *);
char * get_cur_pos(Scanner *);
int scanner_push_state(Scanner **, Scanner *);
Scanner * scanner_pop_state(Scanner **);
#endif
