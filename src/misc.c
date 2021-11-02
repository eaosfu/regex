#include "misc.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

size_t cached_page_size;

__attribute__((constructor))
static void
cache_page_size() {
  cached_page_size = sysconf(_SC_PAGE_SIZE);
}


size_t round_to_page(size_t size) {
  return (((size + (cached_page_size - 1)) / cached_page_size) * cached_page_size);
}


void
parser_fatal(const char * program_name, const char * msg, const char * regex, const char * here)
{
  #define err_buf_sz  201
  char err_msg[err_buf_sz] = {0};
  int sz = snprintf(NULL, 0, "%s: %s: %s", program_name, msg, regex);
  if(sz > err_buf_sz) {
    sz = snprintf(NULL, 0, "%s: %s: %s", program_name, msg, "'regex is too long to be displayed'");
    ++sz;
    snprintf(&(err_msg[0]), sz, "%s: %s: %s\n", program_name, msg, "'regex is too long to be displayed'");
    fprintf(stderr, "%s\n", &(err_msg[0]));
  }
  else {
    snprintf(&(err_msg[0]), sz, "%s: %s: %s", program_name, msg, regex);
    fprintf(stderr, "%s\n", &(err_msg[0]));
    sz = sz - 1 - strlen(regex) + (here - regex);
    for(int i = 0; i < sz; ++i) {
      fprintf(stderr, " ");
    }
    fprintf(stderr, "^\n");
  }
  exit(EXIT_FAILURE);
}


void
fatal(const char * msg)
{
  fprintf(stderr, "ERROR: %s", msg);
  exit(EXIT_FAILURE);
}


inline void *
xmalloc(unsigned int sz)
{
  void * n = calloc(1, sz);
  if(n == NULL) {
    fatal("Out of memeory");
  }
  return  n;
}
