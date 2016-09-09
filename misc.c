#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"

void
fatal(const char * msg)
{
  fprintf(stderr, "%s", msg);
  exit(1);
}


void
warn(const char * msg)
{
  fprintf(stderr, "%s", msg);
}

void *
xmalloc(unsigned int sz) {
  void * n = malloc(sz);
  if(n == NULL) {
    fatal("Out of memeory");
  }
  return  memset(n, 0, sz);
}
