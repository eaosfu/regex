#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void * __real_xmalloc(size_t sz);
void __real_free(void *);
void * __wrap_xmalloc(size_t sz);
void __wrap_free(void *);
