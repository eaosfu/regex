#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"

void
parser_fatal(const char * msg, const char * regex, const char * here)
{
  char err_msg[200] = {0};
  int sz = snprintf(NULL, 0, "%s: %s: %s", program_name, msg, regex);
  snprintf(&(err_msg[0]), sz, "%s: %s: %s", program_name, msg, regex);
  fprintf(stderr, "%s\n", &(err_msg[0]));
  sz = sz - 1 - strlen(regex) + (here - regex);
  for(int i = 0; i < sz; ++i) {
    fprintf(stderr, " ");
  }
  fprintf(stderr, "^\n");
  exit(EXIT_FAILURE);
}

void
fatal(const char * msg)
{
  fprintf(stderr, "ERROR: %s", msg);
  exit(EXIT_FAILURE);
}


void
warn(const char * msg)
{
  fprintf(stderr, "WARNING: %s", msg);
}


inline void *
xmalloc(unsigned int sz) {
  void * n = calloc(1, sz);
  if(n == NULL) {
    fatal("Out of memeory");
  }
  return  n;
}
