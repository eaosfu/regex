#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wrapper_funcs.h"

struct track_ptr {
  void * chunk_id;
  struct track_ptr * next;
  unsigned int chunk_size;
};


struct track_ptr * track_ptr_list = NULL;


void *
__wrap_xmalloc(unsigned int sz)
{
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
