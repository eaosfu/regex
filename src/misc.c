#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"

void
fatal(const char * msg)
{
  fprintf(stderr, "ERROR: %s", msg);
  exit(1);
}


void
warn(const char * msg)
{
  fprintf(stderr, "WARNING: %s", msg);
}


void *
xmalloc(unsigned int sz) {
  void * n = calloc(1, sz);
  if(n == NULL) {
    fatal("Out of memeory");
  }
  return  n;
}
