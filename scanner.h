#include <stdio.h>
#include "token.h"
#include <limits.h>

#define MAX_BUFFER_SZ  256

typedef struct Scanner {
  FILE * input;
  char buffer[MAX_BUFFER_SZ];
  char * readhead;
  int bytes_read;
  int unput;
  int parse_escp_seq;
  Token * curtoken;
} Scanner;

Scanner * init_scanner(FILE *);
static inline int next_char(Scanner *);
void unput(Scanner *);
void reset(Scanner *);
Token * scan(Scanner *);
void free_scanner(Scanner *);
