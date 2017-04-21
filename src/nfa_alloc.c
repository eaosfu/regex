#include "nfa.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

static inline void nfa_alloc_expand(NFAlloc **);

void
nfa_alloc_init(NFACtrl * ctrl, int num)
{
  if(ctrl == NULL || num < 1) {
    return;
  }

  ctrl->alloc = calloc(num, sizeof(NFAlloc));
  ctrl->alloc->pool = calloc(num, sizeof(NFAPool));
  ctrl->alloc->pool_size = num;

  (ctrl->alloc->pool[0]).free = 1;

  if(num == 1) {
    (ctrl->alloc->pool[0]).free = 1;
    (ctrl->alloc->pool[0]).next = &((ctrl->alloc->pool)[0]);
    (ctrl->alloc->pool[0]).prev = &((ctrl->alloc->pool)[0]);
  }
  else {
    (ctrl->alloc->pool[0]).free = 1;
    (ctrl->alloc->pool[0]).next = &((ctrl->alloc->pool)[1]);
    (ctrl->alloc->pool[0]).prev = &((ctrl->alloc->pool)[num-1]);

    NFAPool * prev = ctrl->alloc->pool;
    for(int i = 1; i < num - 1; ++i) {
      (ctrl->alloc->pool[i]).free = 1;
      (ctrl->alloc->pool[i]).next = &((ctrl->alloc->pool)[i + 1]);
      (ctrl->alloc->pool[i]).prev = prev;
      prev = &((ctrl->alloc->pool)[i]);
    }
    (ctrl->alloc->pool[num - 1]).free = 1;
    (ctrl->alloc->pool[num - 1]).next = &((ctrl->alloc->pool)[0]);
    (ctrl->alloc->pool[num - 1]).prev = prev;
  }

  ctrl->alloc->free = ctrl->alloc->pool;
  ctrl->alloc->next = ctrl->alloc;
  ctrl->alloc->prev = ctrl->alloc;

  return;
}


void
nfa_dealloc(NFAlloc * alloc, NFA * nfa)
{
  if(alloc == NULL || nfa == NULL) {
    return;
  }

  size_t offset = offsetof(NFAPool, nfa);

  NFAPool ** free = &(alloc->free);
  NFAPool * released = (NFAPool *)((char *)nfa - offset);

  released->free = 1;

  if(*free == NULL) {
    *free = released;
    (*free)->next = *free;
    (*free)->prev = *free;
  }
  else {
    released->prev = *free;
    released->next = (*free)->next;
    (*free)->next->prev = released;
    (*free)->next = released;
  }

  return;
}


static void
nfa_free_pool(NFAPool * pool, int sz)
{
  if(pool == NULL || sz <= 0) {
    return;
  }

  NFA * nfa = NULL;

  for(int i = 0; i < sz; ++i) {
    nfa = &((pool[i]).nfa);
    if(pool[i].free == 0) {
      if(nfa->value.type == NFA_RANGE) {
        free(nfa->value.range);
      }
      else if(nfa->value.type == NFA_TREE) {
        list_free(nfa->value.branches, NULL);
      }
    }
    list_free_items(&(nfa->reachable), NULL);
  }
  free(pool);
}


void
nfa_free_alloc(NFAlloc ** alloc)
{
  if(alloc == NULL || *alloc == NULL) {
    return;
  }

  NFAlloc * last = (*alloc)->prev;

  while((*alloc) != last) {
    (*alloc)->prev = NULL;
    nfa_free_pool((*alloc)->pool, (*alloc)->pool_size);
    (*alloc)->pool = NULL;
    *alloc = (*alloc)->next;
    (*alloc)->prev->next = NULL;
    free((*alloc)->prev);
  }

  nfa_free_pool((*alloc)->pool, (*alloc)->pool_size);
  (*alloc)->pool = NULL;
  free(*alloc);
  *alloc = NULL;
}


static inline void
nfa_alloc_expand(NFAlloc ** alloc)
{
  int size = (*alloc)->pool_size;
  
  NFAlloc * new_alloc = calloc(1, sizeof(*new_alloc));
  if(new_alloc == NULL) {
// FIXME!
fprintf(stderr, "UNABLE TO REALLOC\n");
    nfa_free_alloc(alloc);
exit(EXIT_FAILURE);
  }
  else {
    new_alloc->prev = (*alloc);
    new_alloc->next = (*alloc)->next;
    (*alloc)->next->prev = new_alloc;
    (*alloc)->next = new_alloc;
    (*alloc) = new_alloc;
  }

  NFAPool * pool = calloc((2 * size), sizeof(*pool));
  if(pool == NULL) {
// FIXME!
fprintf(stderr, "UNABLE TO REALLOC\n");
    nfa_free_alloc(alloc);
exit(EXIT_FAILURE);
  }
  else {
    (*alloc)->pool = pool;
  }

  pool[size].free = 1;

  if(((2 * size) - size) == 1) {
    (pool)[size].prev = &(pool)[size];
    (pool)[size].next = &(pool)[size];
  }
  else {
    (pool)[size].prev = &(pool[2 * size - 1]);
    (pool)[size].next = &(pool[size + 1]);
    NFAPool * prev = &(pool[size]);
    for(int i = size + 1; i < 2 * size; ++i) {
      (pool[i]).free = 1;
      (pool[i]).next = &(pool[i + 1]);
      (pool[i]).prev = prev;
      prev = &(pool[i]);
    }
  }

  (*alloc)->free = &(pool[size]);
  (*alloc)->pool_size = 2 * size;
}


NFA *
nfa_alloc(NFAlloc ** alloc)
{
  if(alloc == NULL || *alloc == NULL) {
    return NULL;
  }

  NFA * ret = NULL;

  NFAPool ** free = &((*alloc)->free);

  if(*free == NULL) {
    nfa_alloc_expand(&(*alloc));
    free = &((*alloc)->free);
  }

  (*free)->free = 0;
  ret = &((*free)->nfa);
  if((*free)->prev == (*free)) {
    *free = NULL;
  }
  else {
    (*free)->next->prev = (*free)->prev;
    (*free)->prev->next = (*free)->next;
    *free = (*free)->next;
  }

  return ret;
}
