#ifndef STACK_H_
#define STACK_H_

#include "slist.h"

#ifndef Stack
typedef List Stack;
#endif

#ifndef StackItem
typedef ListItem StackItem;
#endif

#define new_stack()     new_list()
#define push(s, d)      list_push((s), (d))
#define pop(s)          list_shift((s))
#define peek(s)         list_get_at((s), 0)
#define stack_delete(s, delete_proc) list_free((s), (delete_proc))

#endif
