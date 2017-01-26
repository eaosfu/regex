#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"

void
parser_fatal(const char * msg, const char * regex, const char * here, int adjust)
{
  fprintf(stderr, "ERROR: %s: ", msg);
  fprintf(stderr, "%s\n", regex);
  int finger = snprintf(NULL, 0, "ERROR: %s: ", msg) + here - regex + adjust;
  for(int i = 0; i < finger; ++i) {
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
