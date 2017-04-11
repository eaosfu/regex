#ifndef S_LIST_H_
#define S_LIST_H_

typedef void * VISIT_PROC2(void *, void *);
typedef VISIT_PROC2 * VISIT_PROC2_pt;

typedef void * VISIT_PROC(void *);
typedef VISIT_PROC * VISIT_PROC_pt;

typedef void * COMPARE_PROC(void *, void *);
typedef COMPARE_PROC * COMPARE_PROC_pt;

typedef struct List {
  int size;
  int pool_size;
  int iter_idx;
  struct ListItem * iter;
  struct ListItem * head;
  struct ListItem * pool;
  struct ListItem * tail;
} List;

typedef struct ListItem {
  struct ListItem * next;
  void * data;
} ListItem;


List * new_list();
List * list_chop(List *, unsigned int);
List * list_deep_copy(ListItem *);
List * list_transfer(List *, List *);
List * list_transfer_on_match(List *, List *, VISIT_PROC2_pt, void *);
ListItem * list_reverse(ListItem *);
ListItem * list_get_iterator(List *); // get rid of this!!!

int list_push(List *, void *);
int list_insert_at(List *, void *, int);
int list_append(List *, void *);
int list_set_iterator(List *, int);

void * list_search(List *, void *, COMPARE_PROC_pt);
void * list_shift(List *);
void * list_remove_at(List *, int);
void * list_get_at(List *, int);
void * list_get_head(List *);
void * list_get_tail(List *);
void * list_get_next(List *);
void list_clear(List *);
void list_iterate_from_to(List *, int, int, VISIT_PROC2_pt, void * arg2);
void list_iterate(List *, VISIT_PROC_pt);
void list_iterate2(List *, VISIT_PROC2_pt, void *, void **);
void list_free(List *, VISIT_PROC_pt);
void list_free_items(List *, VISIT_PROC_pt);

#define list_size(l) ((l) ? (l)->size : 0)

#define list_swap(l1, l2)  \
  do {                     \
    List * tmp;            \
    tmp = (l1);            \
    (l1) = (l2);           \
    (l2) = tmp;            \
  } while(0);


#define list_for_each(tmp, list, start, finish) \
  list_set_iterator((list), (start));   \
  for((tmp) = list_get_next((list)); \
      (tmp != NULL) && ((list)->iter_idx <= (finish)); \
      (tmp) = list_get_next((list))\
  )

#endif

