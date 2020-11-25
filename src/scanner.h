#ifndef SCANNER_H_
#define SCANNER_H_

#include "misc.h"
#include "stack.h"
#include "token.h"

#include <stdlib.h>

typedef struct Scanner {
  int line_no;
  int eol_symbol;
  #ifndef ssize_t
  int line_len;
  #else
  ssize_t line_len;
  #endif
  size_t buf_len;
  const char * filename;
  ctrl_flags * ctrl_flags;
  char  * buffer;
  char  * readhead;
  char  * str_begin;
  char  * last_newline;
  Token * curtoken;
} Scanner;


Token * regex_scan(Scanner *);
Scanner * init_scanner(const char *, char *, unsigned int, unsigned int, ctrl_flags *);
void unput(Scanner *);
void free_scanner(Scanner *);
void restart_from(Scanner *, char *);
void reset_scanner(Scanner *, const char *);
int next_char(Scanner *);
char * get_scanner_readhead(Scanner *);
char * get_cur_pos(Scanner *);
int          get_buffer_length(Scanner *);
const char * get_buffer_start(Scanner *);
const char * get_buffer_end(Scanner *);
const char * get_filename(Scanner *);
int get_line_no(Scanner *);
int scanner_push_state(Scanner **, Scanner *);
Scanner * scanner_pop_state(Scanner **);
#endif
