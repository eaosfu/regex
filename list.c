#include <stdio.h>
#include <stdlib.h>
#include "list.h"
#include "misc.h"


// CALLED BY ALLOCATE_FROM_POOL() AND LIST_REMOVE()
static inline ListItem *
remove_node(ListItem ** node_pp)
{
#define NODE      (*node_pp)
#define NODE_NEXT ((*node_pp)->next)
#define NODE_PREV ((*node_pp)->prev)

  ListItem * removed_node = NODE;

  if(NODE_PREV == NODE) {
    node_pp = NULL;
  }
  else {
    NODE_NEXT->prev = NODE_PREV;
    NODE_PREV->next = NODE_NEXT;
  }

#undef NODE
#undef NODE_NEXT
#undef NODE_PREV

  return removed_node;
}


// USED ONLY IN THIS C FILE
static inline ListItem *
allocate_from_pool(List * list)
{
  ListItem * pool_item = remove_node(&(list->pool));
  list->pool_size--;
  return pool_item;
}


static inline ListItem *
new_list_item(List * list, void * data)
{
  ListItem * new_item;

  if(list->pool_size == 0) {
    new_item = xmalloc(sizeof * new_item);
  }
  else {
    new_item = allocate_from_pool(list);
  }
  new_item->data = data;
  new_item->next = new_item;
  new_item->prev = new_item;
  new_item->list = list;
  return new_item;
}


static inline void
reset_head(ListItem ** head_pp, ListItem * item)
{
#define  HEAD (*head_pp)
#define LAST  ((*head_pp)->prev)

  item->next = HEAD;
  item->prev = LAST;
  LAST->next = item;
  HEAD->prev = item;
  HEAD = item;

#undef HEAD
#undef LAST

  return;

}


void
list_release_to_pool(ListItem * item)
{
  if(item->list->pool == NULL) {
    item->list->pool = item;
  }
  else {
    reset_head(&(item->list->pool), item);
  }

  item->list->pool_size++;

  return;
}


// LIST INTERFACE FUNCTIONS
List *
new_list(VISIT_PROC_pt proc)
{
  List * new_list = xmalloc(sizeof * new_list);
  new_list->id = new_list;
  new_list->head = NULL;
  new_list->pool = NULL;
  new_list->delete_data = proc;
  new_list->size = 0;
  new_list->pool_size = 0;
}


ListItem *
list_push(List * list, void * item)
{
  ListItem * new_item = new_list_item(list, item);

  if(list->head != NULL) {
    reset_head(&(list->head), new_item);
  }
  else {
    list->head = new_item;
  }

  list->size++;

  return new_item;
}


ListItem *
list_shift(ListItem * item)
{
  if(item != NULL && item->prev != item) {
    remove_node(&item);
    item->next = item;
    item->prev = item;
  }

  return item;
}


void
list_delete_item(ListItem * item)
{
  if(item->list->delete_data) {
    item->list->delete_data(item);
  }

  if(item->prev != item) {
    list_shift(item);
  }

  free(item);
}


void
list_clear(List * list)
{
#define POOL (list->pool)
#define POOL_LAST (list->pool->prev)
#define POOL_SIZE (list->pool_size)
#define LIST (list->head)
#define LIST_LAST (list->head->prev)
#define LIST_SIZE (list->size)

  if(POOL_SIZE == 0) {
    POOL = LIST;
  }
  else {
    LIST_LAST->next = POOL;
    LIST->prev = POOL_LAST;
    POOL_LAST->next = LIST;
    POOL->prev = LIST;
    POOL = LIST;
  }

  list->pool_size += list->size;
  list->size = 0;

  LIST = NULL;
  
#undef POOL
#undef POOL_LAST
#undef LIST
#undef LIST_LAST
}


// TEST
/*
int
main()
{
  struct { int a; } item = { .a=10};
  struct { int a; } item2 = { .a=20};
  struct { int a; } item3 = { .a=30};
  struct { int a; } item4 = { .a=40};

  List * l = new_list(NULL);
  ListItem * li  = list_push(l, (void *)&item);
  ListItem * li2 = list_push(l, (void *)&item2);
  ListItem * li3 = list_push(l, (void *)&item3);
  list_release_to_pool(li2);
  ListItem * li4 = list_push(l, (void *)&item4);
  list_clear(l);

  return 0;
}
*/
