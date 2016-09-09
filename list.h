#ifndef LIST_H_
#define LIST_H_

typedef void * VISIT_PROC(void *);
typedef VISIT_PROC * VISIT_PROC_pt;

typedef struct List {
  struct List * id;
  struct ListItem * head;
  struct ListItem * pool;
  VISIT_PROC_pt delete_data;
  int size;
  int pool_size;
} List;

typedef struct ListItem {
  List * list;
  struct ListItem * next;
  struct ListItem * prev;
  void * data;
} ListItem;


List * new_list(VISIT_PROC_pt);
ListItem * list_push(List *, void *);
ListItem * list_shift(ListItem *);
void list_clear(List *);
void list_delete_item(ListItem *);
void list_release_to_pool(ListItem *);
void list_delete(List *);

#endif
