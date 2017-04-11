#include "wrapper_funcs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <execinfo.h>

struct track_ptr {
  void * chunk_id;
  struct track_ptr * next;
  unsigned int chunk_size;
};


struct track_ptr * track_ptr_list = NULL;

int total_alloc = 0;


static inline void __attribute__((always_inline))
list_bt(size_t sz)
{
  int bt_sz;
  int max_bt_sz = 10;
  void * buffer[max_bt_sz];
  char ** callers;
  if((bt_sz = backtrace(buffer, max_bt_sz)) != 0) {
    if((callers = backtrace_symbols(buffer, bt_sz)) == NULL) {
      err(EXIT_FAILURE, "backtrace_symbols()");
    }
    for(int i = bt_sz - 1; i > 0; --i) {
      printf("== %d - %s\n", i, callers[i]);
    }
    total_alloc += sz;
    printf("== 0 - %s requested %ld bytes\n\n", callers[0], sz);
    free(callers);
  }
  return;
}


void *
__wrap_xmalloc(size_t sz)
{
#ifdef LIST_BACK_TRACE
  list_bt(sz);
#endif
  void * new_chunk = __real_xmalloc(sz);
  struct track_ptr * new_track_ptr = malloc(sizeof(*new_track_ptr));
  new_track_ptr->chunk_id = new_chunk;
  new_track_ptr->next = NULL;
  new_track_ptr->chunk_size = sz;

  if(track_ptr_list == NULL) {
    track_ptr_list = new_track_ptr;
  }
  else {
    new_track_ptr->next = track_ptr_list;
    track_ptr_list = new_track_ptr;
  }
  return new_chunk;
}


void
__wrap_free(void * ptr)
{
  void * tracker;
  struct track_ptr ** search = &track_ptr_list;

  while((*search) && (*search)->next && ((*search)->chunk_id != ptr)) {
    search = &((*search)->next);
  }

  if((*search) != NULL) {
    tracker = &((*search)->chunk_id);
    (*search) = (*search)->next;
    free(tracker);
  }
   __real_free(ptr);
}
