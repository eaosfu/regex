#include <stdlib.h>
#include "slist.h"
#include "misc.h"

#include <stdio.h>

static inline void
release_to_pool(List * list, ListItem * item)
{
  item->next = NULL;

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
    item = xmalloc(sizeof * item);
  }

  item->data = data;
  item->next = NULL;

  return item;
}


List *
new_list()
{
  List * new_list = xmalloc(sizeof * new_list);
  return new_list;
}



// Insert item at end of list
// return the index at which item was inserted
//ListItem *
int
list_append(List * list, void * data)
{
  if(list == NULL) {
    //return NULL;
    return -1;
  }

  ListItem ** itr = &(list->head);
  unsigned int idx = 0;

  while((*itr)->next && (itr = &((*itr)->next)) && ++idx);
  ++idx;
  (*itr)->next = new_list_item(list, data);
  list->size++;
  //return list->head;
  return idx;
}


// insert new data item at the head of the list
// return the new size of the list
//ListItem *
int
list_push(List * list, void * data)
{
  if(list == NULL) {
    // return NULL;
    return -1;
  }

  ListItem * new_node = new_list_item(list, data);
  new_node->next = list->head;
  list->head = new_node;
  list->size++;

  //return list->head;
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
    //return NULL;
    return -1;
  }

  ListItem ** item_pp = &(list->head);
  int cur_idx = 0;

  while((*item_pp) && (*item_pp)->next && (cur_idx < idx)) {
    item_pp = &((*item_pp)->next);
    cur_idx++;
  }
  
  ListItem * new_item = new_list_item(list, data);
  new_item->next = (*item_pp);
  *item_pp = new_item;
  cur_idx = (idx <= list->size++) ? cur_idx : list->size - 1;

  // return list->head;
  return cur_idx;
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

  ListItem ** item_pp = &(list->head);
  int cur_idx = 0;

  while((*item_pp) && (*item_pp)->next && (cur_idx < idx)) {
    item_pp = &((*item_pp)->next);
    cur_idx++;
  }
  
  ListItem * removed_item = NULL;

//  if(cur_idx == idx) {
    removed_item = (*item_pp);
    (*item_pp) = (*item_pp)->next;
//  }

  list->size--;

  void * removed_data = removed_item->data;

  release_to_pool(list, removed_item);
  
  return removed_data;
  
}


// Return data at location idx
// The list is not modified in any way
void *
list_get_at(List * list, int idx)
{
  if(list == NULL || list->head == NULL || idx < 0) {
    return NULL;
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

  list->pool_size += list->size;
  list->size = 0;

  return;
}


void
list_free(List * list, VISIT_PROC_pt delete_data)
{
  if(list == NULL) {
    return;
  }

  list_clear(list);

  ListItem * cur_item = list->pool;
  ListItem * tmp = NULL;

  while(cur_item->next) {
    tmp = cur_item->next;

    if(delete_data) {
      delete_data(cur_item->data);
    }

    free(cur_item);
    cur_item = tmp;
  }

  free(cur_item);
  free(list);
  list = NULL;

  return;
}


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


/*
int
main(void)
{
#undef list_assert
#define list_assert(a, b)  (((a) == (b)) ? printf("PASS\n") : printf("FAIL\n"))

  List * list = new_list();
  
  struct { int a; } d1 = { .a = 10 };
  struct { int a; } d2 = { .a = 20 };
  struct { int a; } d3 = { .a = 20 };
  struct { int a; } d4 = { .a = 50 };

  list_assert(list_insert_at(list, (void *)&d1, 0),  0);
  list_assert(list_insert_at(list, (void *)&d1, 10), 1);
  list_assert(list_insert_at(list, (void *)&d2, 10), 2);
  list_assert(list_insert_at(list, (void *)&d2, 0),  0);
  list_assert(list_insert_at(list, (void *)&d3, 2),  2);

  list_assert(list_append(list, (void *)&d4), list->size - 1);

  // SHOULD NOT ANYTHING ANYTHING
  list_assert(list_remove_at(list, 10), NULL);

  // REMOVE THE HEAD
  list_assert(list_remove_at(list, 0), (void *)&d2);
  
  // REMOVE MIDDLE ELEMENT
  // void * rm = list_remove_at(list, 1);

  // REMOVE END ELEMENT
  // void * rm = list_remove_at(list, 2);

  // REMOVE THE HEAD
// list_assert(list_shift(list), );
  list_assert(list_get_at(list, -1), NULL);
  list_assert(list_get_at(list, -4), NULL);
// list_assert(list_get_at(list, 0), );
//  list_assert(list_get_at(list, list->size - 1), );
  list_assert(list_get_at(list, 20), NULL);

  list_clear(list);

  list_assert(list->head, NULL);
  list_assert(list->size, 0);

// list_assert(list->pool, );



  list_free(list, NULL);
//  list_assert();

  printf("Hello world\n");
}
*/