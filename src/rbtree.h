#ifndef RB_TREE_H_
#define RB_TREE_H_

typedef void * VISIT_PROC(void *);
typedef VISIT_PROC * VISIT_PROC_pt;
typedef enum {RED = 0, BLACK} color;

#define ROOT(rbt) ((rbt)->root)


typedef struct RBTreeCtrl {
  int count;
  struct RBTree * root;
  struct RBTree * pool_root;
  struct RBTree * pool_ptr;
} RBTreeCtrl;


typedef struct RBTree {
  struct RBTreeCtrl * ctrl;
  void * data;
  int color;
  long key;
  struct RBTree * parent;
  struct RBTree * left;
  struct RBTree * right;
} RBTree;


RBTreeCtrl * new_rbtree();
RBTree * rbtree_search(RBTree *,  long);
RBTree * rbtree_min(RBTree *);
RBTree * rbtree_max(RBTree *);
RBTree * rbtree_predecessor(RBTree *);
RBTree * rbtree_successor(RBTree *);
RBTree * rbtree_insert(RBTreeCtrl *,  long, void *, int);
RBTree * rbtree_insert_reverse(RBTreeCtrl *,  long, void *, int);
void     rbtree_remove(RBTree *);
void     rbtree_free(RBTreeCtrl *, VISIT_PROC_pt);
void     rbtree_clear(RBTreeCtrl *);
int      rbtree_node_count(RBTreeCtrl *);

#endif
