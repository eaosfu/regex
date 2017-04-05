#include "slist.h"
#include "misc.h"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

void *
list_get_tail(List * list)
{
  if((list != NULL) && (list->tail != NULL)) {
    return list->tail->data;
  }
  return NULL;
}


void *
list_get_head(List * list)
{
  if((list != NULL) && (list->head != NULL)) {
    return list->head->data;
  }
  return NULL;
}


List *
list_transfer_on_match(List * dst, List * src, VISIT_PROC2_pt compare, void * arg2)
{
  if(dst == NULL) {
    return NULL;
  }

  if(src == NULL && src->head == NULL) {
    return dst;
  }

  ListItem * tmp = NULL;
  ListItem ** iter = &(src->head);
  void * res =  NULL;

  while((*iter) != NULL) {
    if((res = compare((*iter)->data, arg2)) != NULL) {
      list_append(dst, res);
      tmp = (*iter);
      (*iter) = (*iter)->next;
      free(tmp);
      --(src->size);
      iter = &(*iter);
      continue;
    }
    iter = &((*iter)->next);
  }

  if(list_size(src) == 0) {
    src->head = NULL;
    src->tail = NULL;
    src->iter = NULL;
    src->iter_idx = -1;
  }
  else if(list_size(src) == 1) {
    src->tail = src->head;
    src->iter = src->head;
    src->iter_idx = 0;
  }

  return dst;
}


List *
list_transfer(List * dst, List * src)
{
  if(dst == NULL) {
    return NULL;
  }

  if(src == NULL || src->head == NULL) {
    return dst;
  }

  if(dst->head == NULL) {
    dst->head = src->head;
    dst->tail = src->tail;
  }
  else {
    dst->tail->next = src->head;
    dst->tail = src->tail;
  }

  dst->size += src->size;

  src->head = NULL;
  src->tail = NULL;
  src->iter = NULL;
  src->iter_idx = -1;
  src->size = 0;

  return dst;
}


static void
release_to_pool(List * list, ListItem * item)
{
  item->next = NULL;
  item->data = NULL;

  if(list->pool != NULL) {
    item->next = list->pool;
  }

  list->pool = item;
  list->pool_size++;
  list->size--;

  return;
}


static inline ListItem *
allocate_from_pool(List * list)
{
  ListItem * old_head = list->pool;
  list->pool = old_head->next;
  old_head->next = NULL;
  list->pool_size--;
  return old_head;
}


ListItem *
new_list_item(List * list, void * data)
{
  ListItem * item = NULL;

  if(list->pool_size > 0) {
    item = allocate_from_pool(list);
  }
  else {
    item = xmalloc(sizeof(*item));
  }

  item->data = data;
  item->next = NULL;

  return item;
}


List *
new_list()
{
  List * new_list = xmalloc(sizeof * new_list);
  new_list->iter_idx = -1;
  return new_list;
}



// Insert item at end of list
// return the index at which item was inserted
//ListItem *
int
list_append(List * list, void * data)
{
  if(list == NULL) {
    return -1;
  }

  int idx = 0;
//  ListItem * iter = list->head;

  if(list->head == NULL) {
    list->head = new_list_item(list, data);
    list->tail = list->head;
    list->iter = list->head;
  }
  else {
    list->tail->next = new_list_item(list, data);
    list->tail = list->tail->next;
    idx = list->size;
  }

  ++list->size;

  return idx;
}


// insert new data item at the head of the list
// return the new size of the list
//ListItem *
int
list_push(List * list, void * data)
{
  if(list == NULL) {
    return -1;
  }

  ListItem * new_node = new_list_item(list, data);
  new_node->next = list->head;
  list->head = new_node;

  if(list->size == 0) {
    list->iter = list->tail = list->head;
    list->iter_idx = 0;
  }

  if(list->iter_idx >= 0) {
    ++(list->iter_idx);
  }

  list->size++;

  return list->size;
}


// return data at the head of the list
void *
list_shift(List * list)
{
  if(list == NULL || list->head == NULL) {
    return NULL;
  }

  ListItem * old_head = list->head;
  list->head = list->head->next;

  void * removed_data = old_head->data;

  if(old_head == list->tail) {
    list->tail = NULL;
  }

  --(list->iter_idx);

  release_to_pool(list, old_head);

  return removed_data;
}


// insert new data into list at index idx. If it idx > list->size
// data is inserted at the end of the list;
// return the index the new item was inserted into
//ListItem *
int
list_insert_at(List * list, void * data, int idx)
{
  if(list == NULL || idx < 0) {
    return -1;
  }

  int cur_idx = 0;
  ListItem ** item_pp = &(list->head);

  while((*item_pp) && (*item_pp)->next && (cur_idx < (idx - 1))) {
    item_pp = &((*item_pp)->next);
    cur_idx++;
  }

  ListItem * new_item = new_list_item(list, data);

  if(idx >= list->size) {
    list->tail = new_item;
  }

  if(idx == 0) { // insert new head
    new_item->next = list->head;
    list->head = new_item;
  }
  else if((*item_pp)) { // list was not empty, insert new element
    new_item->next = (*item_pp)->next;
    (*item_pp)->next = new_item;
    ++cur_idx;
  }
  else { // list was empty, new item is now the head
    (*item_pp) = new_item;
  }

  ++(list->size);

  return cur_idx;
}


ListItem *
list_get_iterator(List * list)
{
  if(list == NULL || list->head == NULL) {
    return NULL;
  }

  return list->iter;

}


int
list_set_iterator(List * list, int indx)
{
  if(list == NULL || list->head == NULL || indx >= list_size(list)) {
    return 0;
  }

  if(list->iter_idx == indx) {
    return 1;
  }
  else if((list->iter_idx > indx) || (list->iter_idx < 0)) {
    list->iter = list->head;
    list->iter_idx = 0;
  }

  for(; list->iter_idx != indx; ++(list->iter_idx)) {
    list->iter = list->iter->next;
  }

  return 1;
}


void *
list_get_next(List * list)
{
  if(list == NULL || list->head == NULL || list->iter_idx >= list_size(list) || list->iter_idx < 0) {
    return NULL;
  }

  ListItem * ret = NULL;
  if(list->iter_idx == 0) {
    ret = list->head;
    list->iter = list->head->next;
    ++(list->iter_idx);
  }
  else {
    ret = list->iter;
    list->iter = list->iter->next;
    ++(list->iter_idx);
  }
  return (ret == NULL) ? NULL : ret->data;
}


// Return data at offset idx from the head
// if idx exceeds size of list, or if idx < 0
// return NULL.
// The list is not modified in any way
void *
list_remove_at(List * list, int idx)
{
  if(list == NULL || idx < 0 || idx >= list->size) {
    return NULL;
  }

  ListItem * tmp_tail = list->head;
  ListItem ** item_pp = &(list->head);

  int cur_idx = 0;

  while((*item_pp) && (*item_pp)->next && (cur_idx < idx - 1)) {
    tmp_tail = (*item_pp);
    item_pp = &((*item_pp)->next);
    cur_idx++;
  }

  if(idx > 0) {
    item_pp = &((*item_pp)->next);
  }
  
  ListItem * removed_item = NULL;

  removed_item = (*item_pp);
  (*item_pp) = (*item_pp)->next;

  if(removed_item == list->tail) {
    list->tail = tmp_tail;
  }

  void * removed_data = removed_item->data;

  if(removed_data == list->iter) {
    list->iter_idx = -1;
  }

  release_to_pool(list, removed_item);
  
  return removed_data;
  
}


// Return data at location idx
// The list is not modified in any way
void *
list_get_at(List * list, int idx)
{
  if(list == NULL || list->head == NULL || idx < 0 || idx >= list->size) {
    return NULL;
  }
  
  if(idx == 0) {
    return list->head->data;
  }

  if(idx == (list->size - 1)) {
    return list->tail->data;
  }

  ListItem * iter = list->head;
  void * data = NULL;
  int cur_idx = 0;

  while((cur_idx < idx) && iter->next) {
    cur_idx++;
    iter = iter->next;
  }

  if(cur_idx == idx) {
    data = iter->data;
  }

  return data;
}


void 
list_clear(List * list)
{
  if(list == NULL || list->size == 0) {
    return;
  }

  if(list->pool_size == 0) {
    list->pool = list->head;
  }
  else {
    ListItem * dst = list->pool;
    ListItem * src = list->head;

    if(list->size < list->pool_size) {
      dst = list->head;
      src = list->pool;
    }
    
    ListItem * start = dst;

    while(dst->next) {
      dst = dst->next;
    }

    dst->next = src;

    src = NULL;

    list->pool = start;
  }

  list->head = NULL;
  list->tail = NULL;
  list->iter = NULL;

  list->pool_size += list->size;
  list->size = 0;
  list->iter_idx = -1;

  return;
}


void
list_free_items(List * list, VISIT_PROC_pt delete_data)
{
  if(list == NULL) {
    return;
  }

  list_clear(list);

  ListItem * cur_item = list->pool;
  ListItem * tmp = NULL;

  while(cur_item && cur_item->next) {
    tmp = cur_item->next;

    if(delete_data) {
      delete_data(cur_item->data);
    }

    --(list->pool_size);
    free(cur_item);
    cur_item = tmp;
  }

  // remove last item and its data
  if(cur_item) {
    if(delete_data && cur_item->data) {
      delete_data(cur_item->data);
    }
    free(cur_item);
  }

  return;
}


void
list_free(List * list, VISIT_PROC_pt delete_data)
{
  if(list == NULL) {
    return;
  }
  
  list_free_items(list, delete_data);

  (list)->head = NULL;
  (list)->pool = NULL;
  (list)->tail = NULL;
  free(list);
  list = NULL;

  return;
}


// 'deep-copy' just means we copy data-pointers
List *
list_deep_copy(ListItem * list_node)
{
  if(list_node == NULL) {
    return NULL;
  }

  List * list_copy = xmalloc(sizeof * list_copy);

  while(list_node) {
    list_append(list_copy, list_node->data);
    list_node = list_node->next;
  }

  return list_copy;
}


// Reverse the direction the 'next' pointers
// points in.
// Does not take into consideration order of the
// data
// FIX SO THAT POINTER PASSED IN IS POINTER TO THE HEAD POINTER, THEN WHEN END OF LIST IS FOUND
// HEAD POINTER IS MADE TO POINT TO THAT NODE
ListItem *
list_reverse(ListItem * node)
{
  if(node == NULL) {
    return node;
  }

  ListItem * prev_node = NULL;
  ListItem * next_node = node->next;

  while(next_node != NULL) {
    node->next = prev_node;
    prev_node = node;
    node = next_node;
    next_node = next_node->next;

    if(next_node == NULL) {
      node->next = prev_node;
    }
  }

  return node;
}


void *
list_search(List * list, void * target, COMPARE_PROC_pt compare)
{
  void * ret = NULL;

  if(list == NULL || list->head == NULL) {
    return ret;
  }

  if(!compare) {
    fatal("No comparison function provided\n");
  }

  ListItem * iter = list->head;
  int idx = 0;
  while((ret = compare(iter->data, target)) == NULL && iter->next) {
    iter = iter->next;
    ++idx;
  }

  return ret;
}


// 0 indexed
List *
list_chop(List * list, unsigned int sz)
{
  List * chopped = NULL;

  if(list == NULL || list->head == NULL || sz < 1) {
    return chopped;
  }

  chopped = new_list();
  chopped->head = list->head;

  if(sz >= list->size) {
    chopped->size = list->size;
    chopped->tail = list->tail;
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
  }
  else {
    chopped->size = sz;
    ListItem ** new_head = &(list->head);
    ListItem * chop_tail = *new_head;
    for(int i = 0; i < sz; ++i) {
      chop_tail = *new_head;
      new_head = &((*new_head)->next);
    }
    list->head = (*new_head);
    list->size -= sz;
    chopped->tail = chop_tail;
    (*new_head) = NULL;
  }

  return chopped;
}


void
list_iterate_from_to(List * list, int from, int to, VISIT_PROC2_pt action, void *arg2)
{
  if(list == NULL || from < 0 || (to - from) < 0 || to < 0 || to >= list->size) {
    return;
  }

  if(list->iter == NULL || list->iter_idx < 0 || from < list->iter_idx || list->iter_idx > to) {
    list->iter_idx = 0;
    list->iter = list->head;
  }

  if(list->iter_idx < from) {
    while(list->iter_idx != from) {
      ++(list->iter_idx);
      list->iter = list->iter->next;
    }
  }

  ListItem ** li = &(list->iter);
  //for(int i = from; i <= to; ++i && ++(list->iter_idx)) {
  for(int i = from; i <= to; ++i, ++(list->iter_idx)) {
    action((void *)((*li)->data), arg2);
    (*li) = (*li)->next;
  }

  return;
}


void
list_iterate(List * list, VISIT_PROC_pt action)
{
  if(list == NULL) {
    return;
  }

  ListItem * li = list->head;
  int upper_bound = list->size;
  for(int i = 0; i < upper_bound; ++i) {
    action((void *)(li->data));
    li = li->next;
  }

  return;
}


void
list_iterate2(List * list, VISIT_PROC2_pt action, void * extra_arg, void ** ret)
{
  if(list == NULL) {
    return;
  }

  ListItem * li = list->head;
  int upper_bound = list->size;
  for(int i = 0; i < upper_bound; ++i) {
    (*ret) = action((void *)(li->data), extra_arg);
    li = li->next;
  }

  return;
}
