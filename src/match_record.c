#include "misc.h"
#include "match_record.h"

#include <stdlib.h>


MatchRecordObj *
new_match_record_obj()
{
  return xmalloc(sizeof(MatchRecordObj));
}


void
new_match_record(MatchRecordObj * obj, char * beg, char * end)
{
  if(obj == NULL || beg == NULL || end == NULL) {
    return;
  }

  MatchRecord * new_match;

  if(obj->pool == NULL) {
    new_match = malloc(sizeof(*new_match));
  }
  else {
    new_match = obj->pool;
    if(obj->pool->next == obj->pool) {
      obj->pool = NULL;
    }
    else {
      obj->pool->prev->next = obj->pool->next;
      obj->pool->next->prev = obj->pool->prev;
      obj->pool = obj->pool->next;
    }
  }

  if(obj->head == NULL) {
    new_match->next = new_match;
    new_match->prev = new_match;
    obj->head = new_match;
  }
  else {
    new_match->next = obj->head;
    new_match->prev = obj->head->prev;
    obj->head->prev->next = new_match;
    obj->head->prev = new_match;
  }

  new_match->beg = beg;
  new_match->end = end;
  ++(obj->match_count);

  return;
}


void
match_record_clear(MatchRecordObj * obj)
{
  if(obj == NULL || obj->head == NULL) {
    return;
  }

  if(obj->pool == NULL) {
    obj->pool = obj->head;
  }
  else {
    MatchRecord * tmp;
    if(obj->head->next == obj->head) {
      obj->head->prev = obj->pool->prev;
      obj->pool->prev->next = obj->head;
      obj->head->next = obj->pool;
      obj->pool->prev = obj->head;
    }
    else {
      // connect head and pool by making a figure 8
      tmp = obj->head->prev;
      obj->head->prev->next = obj->pool;
      obj->pool->prev->next = obj->head;
      obj->head->prev = obj->pool->prev;
      obj->pool->prev = tmp;
    }
  }

  obj->head = NULL;
  obj->match_count = 0;
}


void
match_record_free(MatchRecordObj ** obj)
{
  if(obj == NULL || *obj == NULL) {
    return;
  }

  match_record_clear(*obj);

  MatchRecord * last = (*obj)->pool->prev;
  MatchRecord ** pool = &((*obj)->pool);

  while((*pool) != last) {
    (*pool)->prev = NULL;
    *pool = (*pool)->next;
    (*pool)->prev->next = NULL;
    free((*pool)->prev);
  }

  (*pool)->prev = NULL;
  (*pool)->next = NULL;
  free(*pool);
  *pool = NULL;

  free(*obj);
  *obj = NULL;
  return;
}
