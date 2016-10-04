#ifndef S_LIST_H_
#define S_LIST_H_

typedef void * VISIT_PROC(void *);
typedef VISIT_PROC * VISIT_PROC_pt;

typedef void * COMPARE_PROC(void *, void *);
typedef COMPARE_PROC * COMPARE_PROC_pt;

typedef struct List {
  struct ListItem * head;
  struct ListItem * tail;
  int size;
// These are temporary, ideally I'd like to have a memmory management
// system in place, but using these will do for now
  int pool_size;
  struct ListItem * pool;
} List;

typedef struct ListItem {
  struct ListItem * next;
  void * data;
} ListItem;


List * new_list();
List * list_deep_copy(ListItem *);
ListItem * list_reverse(ListItem *);

int list_push(List *, void *);
int list_insert_at(List *, void *, int);
int list_append(List *, void *);

void * list_search(List *, void *, COMPARE_PROC_pt);
void * list_shift(List *);
void * list_remove_at(List *, int);
void * list_get_at(List *, int);
void * list_extend(List *, void *);
void list_clear(List *);
void list_free(List *, VISIT_PROC_pt);


#define list_swap(l1, l2)  \
  do {                     \
    List * tmp;            \
    tmp = (l1);            \
    (l1) = (l2);           \
    (l2) = tmp;            \
  } while(0);

#endif