#include "rbtree.h"

#include <stdio.h>
#include <stdlib.h>



#define xmalloc(sz) (calloc(1, (sz)));

static RBTree * new_rbtree_node(RBTreeCtrl *,  long, void *, int);
static void left_rotate(RBTree *);
static void right_rotate(RBTree *);
static void rbtree_fixup(RBTree *);
static void rbtree_remove_fixup(RBTree *);

int
rbtree_node_count(RBTreeCtrl * t)
{
  if(t == NULL) {
    return 0;
  }
  return t->count;
}


RBTreeCtrl *
new_rbtree()
{
  RBTreeCtrl * ctrl = xmalloc(sizeof(*ctrl));
  return ctrl;
}


static inline void __attribute__((always_inline))
reset_pool_ptr(RBTree ** ptr)
{
  while(((*ptr) != NULL) && (*ptr)->left == NULL) {
    if((*ptr)->right == NULL) break;
    *ptr = (*ptr)->right;
  }

  while(((*ptr) != NULL) && (*ptr)->left != NULL) {
    *ptr = (*ptr)->left;
  }
}


void
rbtree_clear(RBTreeCtrl * t)
{
  if(t == NULL || t->root == NULL) {
    return;
  }

  if(t->pool_root == NULL) {
    t->pool_root = t->root;
  }
  else {
    t->pool_ptr->left = t->root;
    t->root->parent = t->pool_ptr;
  }

  t->pool_ptr = rbtree_min(t->root);
  reset_pool_ptr(&(t->pool_ptr));

  t->count = 0;

  t->root = NULL;

  return;
}


static inline RBTree *
new_rbtree_node(RBTreeCtrl * tree, long key, void * data, int reuse_data)
{
  RBTree * tn = NULL;
  if(tree->pool_ptr != NULL) {
    tn = tree->pool_ptr;
    if(tree->pool_ptr->parent == NULL) {
      tree->pool_ptr = tree->pool_ptr->right;
      tree->pool_root = tree->pool_ptr;
      if(tree->pool_ptr != NULL) {
        tree->pool_ptr->parent = NULL;
        reset_pool_ptr(&(tree->pool_ptr));
      }
    }
    else {
      if(tree->pool_ptr == tree->pool_ptr->parent->left) {
        tree->pool_ptr->parent->left = NULL;
        tree->pool_ptr = tree->pool_ptr->parent;
        if(tree->pool_ptr->right != NULL) {
          tree->pool_ptr = tree->pool_ptr->right;
          reset_pool_ptr(&(tree->pool_ptr));
        }
      }
      else {
        tree->pool_ptr->parent->right = NULL;
        tree->pool_ptr = tree->pool_ptr->parent;
      }
    }
  }
  else {
    tn = xmalloc(sizeof(RBTree));
  }

  if(reuse_data == 0) {
    tn->data = data;
  }

  tn->ctrl = tree;
  tn->key  = key;
  tn->left = NULL;
  tn->right = NULL;
  tn->parent = NULL;

  return tn;
}


static void
rbtree_free_pool(RBTreeCtrl * t, VISIT_PROC_pt free_data)
{
  if(t == NULL || t->pool_root == NULL) {
    return;
  }

  RBTree * pool_root = t->pool_root;
  // pool_ptr points the left most node in the tree
  RBTree * ptr = t->pool_ptr;
  RBTree * tmp = NULL;
  while(ptr != NULL) {
    if(ptr->parent == NULL) {
      ptr = ptr->right;
      reset_pool_ptr(&(ptr));
    }
    else {
      if(ptr == ptr->parent->left) {
        tmp = ptr;
        ptr = ptr->parent;
        if(free_data != NULL) {
          free(tmp->data);
        }
        free(tmp);
        tmp = NULL;
        ptr->left = NULL;
        if(ptr->right != NULL) {
          ptr = ptr->right;
          reset_pool_ptr(&(ptr));
        }
      }
      else {
        tmp = ptr;
        ptr = ptr->parent;
        if(free_data != NULL) {
          free(tmp->data);
        }
        free(tmp);
        tmp = NULL;
        ptr->right = NULL;
      }
    }
  }

  if(free_data != NULL) {
    free(pool_root->data);
  }
  free(t->pool_root);
}


RBTree *
rbtree_search(RBTree * t,  long key)
{
  while(t != NULL && t->key != key) {
    if(key < t->key) {
      t = t->left;
    }
    else {
      t = t->right;
    }
  }
  return (t == NULL) ? NULL : t;
}


static void
rbtree_fixup(RBTree * t)
{
  RBTree * child   = t;
  RBTree * parent  = child->parent;
  RBTree * gparent = NULL;
  RBTree * uncle   = NULL;

  // if parent == NULL --> its color is BLACK
  while(parent != NULL && parent->color == RED) {
    // if parent:RED then it must have a grand parent
    gparent = parent->parent;
    if(parent == gparent->left) { // parent is left child
      uncle = gparent->right; // may be NULL (i.e colored BLACK)
      if(uncle != NULL && uncle->color == RED) {
        // parent:RED && uncle:RED
        parent->color = BLACK;
        uncle->color = BLACK;
        gparent->color = RED;
        child = gparent;
        parent = child->parent; // set parent for the next iteration
      }
      else {
        if(child == parent->right) {
          // parent:RED && uncle:BLACK
          child = parent;
          left_rotate(child);
          parent = child->parent;
          gparent = parent->parent;
        }
        parent->color = BLACK;
        gparent->color = RED;
        right_rotate(gparent);
      }
    }
    else { // parent is right child
      uncle = gparent->left; // may be NULL (i.e colored BLACK)
      if(uncle != NULL && uncle->color == RED) {
        // parent:RED && uncle:RED
        parent->color = BLACK;
        uncle->color = BLACK;
        gparent->color = RED;
        child = gparent;
        parent = child->parent; // set parent for the next iteration
      }
      else {
        if(child == parent->left) {
          // parent:RED && uncle:BLACK
          child = parent;
          right_rotate(child);
          parent = child->parent;
          gparent = parent->parent;
        }
        parent->color = BLACK;
        gparent->color = RED;
        left_rotate(gparent);
      }
    }
  }
  
  if(parent == NULL) {
    t->ctrl->root = child;
    child->color = BLACK;
  }

  return;
}


RBTree *
rbtree_insert(RBTreeCtrl * t,  long key, void * data, int reuse_data)
{
  if(t == NULL) {
    return NULL;
  }

  ++(t->count);
  RBTree * new  = new_rbtree_node(t, key, data, reuse_data);
  RBTree * walker = t->root;
  RBTree * prev = NULL;

  while(walker != NULL) {
    prev = walker;
    if(walker->key < key) {
      walker = walker->right;
    }
    else {
      walker = walker->left;
    }
  }

  new->parent = prev;

  if(prev == NULL) {
    // inserted new node into an empty tree
    t->root = new;
    t->root->color = BLACK;
  }
  else {
    if(prev->key < key) {
      prev->right = new;
    }
    else {
      prev->left = new;
    }
    new->color = RED;
    rbtree_fixup(new);
  }
  return new;
}


RBTree *
rbtree_insert_reverse(RBTreeCtrl * t,  long key, void *data, int reuse_data)
{
  if(t == NULL) {
    return NULL;
  }

  RBTree * new  = new_rbtree_node(t, key, data, reuse_data);
  RBTree * walker = t->root;
  RBTree * prev = NULL;

  while(walker != NULL) {
    prev = walker;
    if(walker->key > key) {
      walker = walker->right;
    }
    else {
      walker = walker->left;
    }
  }

  new->parent = prev;

  if(prev == NULL) {
    // inserted new node into an empty tree
    t->root = new;
    t->root->color = BLACK;
  }
  else {
    if(prev->key > key) {
      prev->right = new;
    }
    else {
      prev->left = new;
    }
    new->color = RED;
    rbtree_fixup(new);
  }
  return new;
}


// free memory used up by entire tree
void
rbtree_free(RBTreeCtrl * t, VISIT_PROC_pt free_data)
{
  if(t == NULL) {
    return;
  } 

  if(t->root != NULL) {
    RBTree * root = t->root;
    RBTree * cur = rbtree_min(root);
    RBTree * next = NULL;

    if(cur == root) {
      cur = rbtree_min(root->right);
    }

    while((cur != NULL)) {
      if(cur == root) {
        if(root->right != NULL) {
          cur = rbtree_min(root->right);
        }
        else {
          break;
        }
      }
      if(cur->left == NULL && cur->right == NULL) {
        if((next = cur->parent) != NULL) {
          if(cur == next->left) {
            next->left = NULL;
          }
          else {
            next->right = NULL;
          }
        }
        if(free_data != NULL) {
          free_data(cur->data);
        }
        free(cur);
        cur = next;
      }
      else if(cur->right != NULL) {
        cur = rbtree_min(cur->right);
      }
    }
    if(free_data != NULL) {
      free_data(root->data);
    }
    free(root);
    t->root = NULL;
  }

  reset_pool_ptr(&(t->pool_ptr));
  rbtree_free_pool(t, free_data);
  free(t);

  return;
}


RBTree *
rbtree_min(RBTree * t)
{
  while((t != NULL) && (t->left != NULL)) {
    t = t->left;
  }
  return (t == NULL) ? NULL : t;
}


RBTree *
rbtree_max(RBTree * t)
{
  while(t && (t->right != NULL)) {
    t = t->right;
  }
  return (t == NULL) ? NULL : t;
}


RBTree *
rbtree_predecessor(RBTree * t)
{
  if(t == NULL) {
    return NULL;
  }

  RBTree * ret = NULL;

  if(t->left != NULL) {
    return rbtree_max(t->left);
  }
  else {
    ret = t->parent;
    if(ret) {
      if(t == ret->left) {
        // traverse up the tree until we find a node
        // that is the right child of its parent
        while((ret != NULL) && (t != ret->right)) {
          t = ret;
          ret = ret->parent;
        }
      }
      // if t is the right child of it's parent then the parent
      // is the immediate predecessor
    }
    // if ret == NULL then tree is only the root since this is the only
    // node that NULL as a parent
  }
  return ret;
}


RBTree *
rbtree_successor(RBTree * t)
{
  if(t == NULL) {
    return NULL;
  }

  RBTree * ret = NULL;

  if(t->right != NULL) {
    ret = rbtree_min(t->right);
  }
  else {
    ret = t->parent;
    while((ret != NULL) && (t == ret->right)) {
      t = ret;
      ret = ret->parent;
    }
  }

  return ret;
}


static void
left_rotate(RBTree * t)
{
  if(t == NULL) {
    return;
  }

  RBTree * tmp = t->right;
  t->right = tmp->left;
  if(tmp->left != NULL) {
    tmp->left->parent = t;
  }
  if(t->parent != NULL) {
    if(t == t->parent->left) {
      t->parent->left = tmp;
    }
    else {
      t->parent->right = tmp;
    }
  }
  else {
    t->ctrl->root = tmp;
  }
  tmp->parent = t->parent;
  tmp->left = t;
  t->parent = tmp;
  return;
}


static void
right_rotate(RBTree * t)
{
  if(t == NULL) {
    return;
  }

  RBTree * tmp = t->left;
  t->left = tmp->right;
  if(tmp->right != NULL) {
    tmp->right->parent = t;
  }
  if(t->parent != NULL) {
    if(t == t->parent->right) {
      t->parent->right = tmp;
    }
    else {
      t->parent->left = tmp;
    }
  }
  else {
    t->ctrl->root = tmp;
  }
  tmp->parent = t->parent;
  tmp->right = t;
  t->parent = tmp;
  return;
}


static void
rbtree_transplant(RBTree * u, RBTree * v)
{
  if(u != NULL) {
    if(u->parent == NULL) {
      u->ctrl->root = v;
    }
    else if(u == u->parent->left) {
      u->parent->left = v;
    }
    else {
      u->parent->right = v;
    }
  }

  if(v != NULL) {
    v->parent = (u == NULL) ? NULL : u->parent;
  }

  return;
}


// remove a node from the tree
static void
rbtree_remove_fixup(RBTree * t)
{
  if(t == NULL) {
    return;
  }

  RBTree * root = t->ctrl->root;
  RBTree * tmp = NULL;
  while((t != root) && (t->color == BLACK)) {
    if(t == t->parent->left) {
      tmp = t->parent->right;
      if(tmp->color == RED) {
        tmp->color = BLACK;
        t->parent->color = RED;
        left_rotate(t->parent);
        tmp = t->parent->right;
      }
      if((tmp->left->color == RED) && (tmp->right->color == BLACK)) {
        tmp->color = RED;
        t = t->parent;
      }
      else if(tmp->right->color == BLACK) {
        tmp->left->color = BLACK;
        tmp->color = RED;
        right_rotate(tmp);
        tmp = t->parent->right;
        tmp->color = t->parent->color;
        t->parent->color = BLACK;
        tmp->right->color = BLACK;
        left_rotate(t->parent);
        t = t->ctrl->root;
      }
    }
    else {
      tmp = t->parent->left;
      if(tmp->color == RED) {
        tmp->color = BLACK;
        t->parent->color = RED;
        left_rotate(t->parent);
        tmp = t->parent->left;
      }
      if((tmp->right->color == RED) && (tmp->left->color == BLACK)) {
        tmp->color = RED;
        t = t->parent;
      }
      else if(tmp->left->color == BLACK) {
        tmp->right->color = BLACK;
        tmp->color = RED;
        right_rotate(tmp);
        tmp = t->parent->left;
        tmp->color = t->parent->color;
        t->parent->color = BLACK;
        tmp->left->color = BLACK;
        left_rotate(t->parent);
        t = t->ctrl->root;
      }
    }
  }
}


void
rbtree_remove(RBTree * t)
{
  if(t == NULL) {
    return;
  }

  RBTree * tmp = t;     // y
  RBTree * tmp2 = NULL; // x
  int orig_color = tmp->color;

  if(t->left == NULL) {
    tmp2 = t->right;
    rbtree_transplant(t, t->right);
  }
  else if(t->right == NULL) {
    tmp2 = t->left;
    rbtree_transplant(t, t->left);
  }
  else {
    tmp = rbtree_min(t->right);
    orig_color = tmp->color;
    tmp2 = tmp->right;
    if(tmp->parent == t) {
      if(tmp2 != NULL)
        tmp2->parent = tmp;
    }
    else {
      rbtree_transplant(tmp, tmp->right);
      tmp->right = t->right;
      t->right->parent = tmp;
    }
    rbtree_transplant(t, tmp);
    tmp->left = t->left;
    tmp->left->parent = tmp;
    tmp->color = t->color;
  }

  if(orig_color == BLACK) {
    rbtree_remove_fixup(tmp2);
  }

  return;
}
