#include "nfa.h"
#include "stack.h"
#include "misc.h"
#include "collations.h"

#include <stdlib.h>
#include <string.h>

extern int ncoll;
extern named_collations collations[];

NFACtrl *
new_nfa_ctrl()
{
  NFACtrl * nfa_ctrl = xmalloc(sizeof * nfa_ctrl);
  nfa_ctrl->free_range = new_list();
  nfa_ctrl->ctrl_id = nfa_ctrl;
  nfa_ctrl->next_seq_id = 1;
  nfa_ctrl->next_interval_seq_id = 0;
  return nfa_ctrl;
}


static inline void
mark_interval_nfa(NFA * nfa)
{
  if(nfa->id == 0) {
    nfa->id = nfa->ctrl->next_interval_seq_id;
    ++(nfa->ctrl->next_interval_seq_id);
  }
}


void
mark_nfa(NFA * nfa)
{
  if(nfa->id == 0) {
    nfa->id = nfa->ctrl->next_seq_id;
    ++(nfa->ctrl->next_seq_id);
  }
}


NFA *
new_nfa(NFACtrl * ctrl, unsigned int type)
{
  if(ctrl == NULL || ctrl->alloc == NULL) {
    return NULL;
  }

  NFA * nfa = nfa_alloc(&(ctrl->alloc));
  nfa->id = 0;
  nfa->ctrl = ctrl;
  nfa->value.type = type;
  nfa->parent = nfa->out1 = nfa->out2 = NULL;

  return nfa;
}


int
update_range_w_collation(char * collation_string, int coll_name_len, NFA * range_nfa, int negate)
{
  int ret = 0;

#define low_bound(i, j)  collations[(i)].ranges[(j)].low
#define high_bound(i, j) collations[(i)].ranges[(j)].high
#define NAMEQ(i) \
  (strncmp(collation_string, collations[(i)].name, coll_name_len))

  for(int i = 0; i < ncoll; ++i) {
    if(NAMEQ(i) == 0) {
      for(int j = 0; j < collations[i].range_num; ++j) {
        update_range_nfa(low_bound(i,j),high_bound(i,j), range_nfa, negate);
      }
      ret = 1;
      break;
    }
  }

#undef collation_low
#undef collation_high
#undef NAMEQ
  return ret;
}


void
update_range_nfa(unsigned int low, unsigned int high, NFA * range_nfa, int negate)
{
  if(negate) {
    for(int i = low; i <= high; ++i) {
      z_clear_bit_array(BIT_MAP_TYPE, *(range_nfa->value.range), BITS_PER_BLOCK, i);
    }
  }
  else {
    for(int i = low; i <= high; ++i) {
      z_set_bit_array(BIT_MAP_TYPE, *(range_nfa->value.range), BITS_PER_BLOCK, i);
    }
  }
}


NFA *
new_range_nfa(NFACtrl * ctrl, int negate)
{
  NFA * start  = new_nfa(ctrl, NFA_RANGE);
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);
  
  mark_nfa(start);

  start->value.range = xmalloc(sizeof *(start->value.range));

  if(negate) {
    for(int i = 0; i < SIZE_OF_RANGE; ++i) {
      (*(start->value.range))[i] = ~((BIT_MAP_TYPE)0x0);
    }
  }

  accept->parent = start;

  start->out1 = start->out2 = accept;

   return accept;
}

/*
NFA *
new_lliteral_nfa(NFACtrl * ctrl, char * src, unsigned int len)
{
  NFA * start    = new_nfa(ctrl, NFA_EPSILON);
  NFA * lliteral = xmalloc(sizeof(*lliteral)+ len + 1);
  NFA * accept   = new_nfa(ctrl, NFA_ACCEPTING);

  start->out1 = start->out2 = lliteral;

  lliteral->ctrl = ctrl;
  mark_nfa(lliteral);
  start->id = lliteral->id;
  lliteral->out1 = lliteral->out2 = accept;
  lliteral->value.type = NFA_LONG_LITERAL;
  lliteral->value.len = len;
  lliteral->value.lliteral = ((char *)(lliteral) + sizeof(*lliteral));
  memcpy((void *)(lliteral->value.lliteral), src, len);
  (lliteral->value.lliteral)[len] = '\0';
  

  accept->parent = start;

  return accept;
}
*/
NFA *
new_lliteral_nfa(NFACtrl * ctrl, char * src, unsigned int len)
{
  NFA * right = NULL;
  NFA * left =  new_literal_nfa(ctrl, src[0], NFA_LITERAL);
  for(int i = 1; i < len; ++i) {
    right =  new_literal_nfa(ctrl, src[i], NFA_LITERAL);
    left = concatenate_nfa(left, right);
  }

  return left;
}

NFA *
new_literal_nfa(NFACtrl * ctrl, unsigned int literal, unsigned int type)
{
  NFA * start;
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);

  start = new_nfa(ctrl, type);

  mark_nfa(start);

  start->value.literal = literal;
  start->out1 = start->out2 = accept;

  accept->parent = start;

  return accept;
}


NFA *
new_backreference_nfa(NFACtrl * ctrl, unsigned int capture_group_id)
{
  NFA * start  = new_nfa(ctrl, NFA_BACKREFERENCE);
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);

  start->id = capture_group_id;
  start->out1 = start->out2 = accept;

  accept->parent = start;

  return accept;
}


NFA *
new_kleene_nfa(NFA * body)
{
  NFA * start  = new_nfa(body->ctrl, NFA_SPLIT);
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);

  start->id = body->parent->id;
  start->value.literal = '?';
  start->out1 = accept;
  start->out2 = body->parent;

  body->value.type = NFA_SPLIT;
  body->value.literal = '*';
  body->out2 = accept;
  // tighten loop
  NFA * target = body->parent;
  while((target->value.type == NFA_EPSILON) && (target = target->out2));
  body->out1 = target;

  accept->parent = start;

  return accept;
}


NFA *
new_qmark_nfa(NFA * body)
{
  NFA * start  = new_nfa(body->ctrl, NFA_SPLIT);
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);
  
  start->id = body->parent->id;
  start->out1 = accept;
  start->out2 = body->parent;
  start->value.literal = '?';

  body->value.type = NFA_EPSILON;
  body->out1 = accept;
  body->out2 = accept;

  accept->parent = start;

  return accept;
}


NFA *
new_posclosure_nfa(NFA * body)
{
  NFA * start  = new_nfa(body->ctrl, NFA_EPSILON);
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);

  start->id = body->parent->id;
  // there is no direct transition to the accepting state
  start->out1 = start->out2 = body->parent;

  body->value.type = NFA_SPLIT;
  body->value.literal = '+';
  // tighten loop
  NFA * target = body->parent;
  while((target->value.type == NFA_EPSILON) && (target = target->out2));
  body->out1 = target;

  body->out2 = accept;
  accept->parent = start;

  return accept;
}


NFA *
new_interval_nfa(NFA * target, unsigned int min, unsigned int max, NFA ** t_reachable, NFA ** ret_interval)
{
  NFA * start = new_nfa(target->ctrl, NFA_EPSILON);
  NFA * accept = new_nfa(target->ctrl, NFA_ACCEPTING);
  NFA * new_interval = new_nfa(target->ctrl, NFA_INTERVAL);

  if(min == 0) {
    start->value.type = NFA_SPLIT;
    start->value.literal = '?';
    start->out1 = target->parent;
    start->out2 = accept;
    ++min;
  }
  else {
    start->out1 = start->out2 = target->parent;
  }
  start->id = target->parent->id;
  target->value.type = NFA_EPSILON;
  target->out2 = new_interval;

  mark_interval_nfa(new_interval);

  new_interval->value.min_rep = min;
  new_interval->value.max_rep = max;
  new_interval->value.split_idx = 0;
  NFA * tmp = target->parent;

  while((tmp->value.type == NFA_EPSILON) && (tmp = tmp->out2));
  new_interval->out1 = tmp;

  *t_reachable = tmp;
  *ret_interval = new_interval;

  new_interval->out2 = accept;
  accept->parent = start;

  return accept;
}


static void *
is_nfa_tree(void * arg, void *arg2)
{
  if(arg == NULL) {
    return NULL;
  }

  NFA * nfa = arg;
  void * ret = NULL;
  if(nfa->parent != NULL) {
    if(nfa->parent->value.type == NFA_TREE) {
      if(arg2 != NULL) {
        nfa->value.type = NFA_EPSILON;
        nfa->out2 = arg2;
      }
      ret = nfa->parent->value.branches;
      //free(nfa->parent);
      nfa_dealloc(nfa->ctrl->alloc, nfa->parent);
    }
  }
  return ret;
}


//
// FUNCTION: new_alternation_nfa:
// INPUT:
//  - body -- type: NFA  *
// OUTPUT:
//  - finsih -- type: NFA *
// SYNOPSIS:
//  
NFA *
new_alternation_nfa(NFACtrl * ctrl, List * branches_list, unsigned int num_branches, NFA * terminator)
{
  if(num_branches < 2) {
    return pop(branches_list);
  }

  NFA * start  = new_nfa(ctrl, NFA_TREE);
  if(terminator == NULL) {
    terminator = new_nfa(ctrl, NFA_ACCEPTING);
    terminator->parent = terminator;
  }

  start->value.branches = list_chop(branches_list, num_branches);

  List * tmp = new_list();
  list_transfer_on_match(tmp, start->value.branches, is_nfa_tree, terminator->parent);

  ListItem * li = start->value.branches->head;
  for(int i = 0; (i < num_branches) && (li != NULL); ++i) {
    ((NFA *)(li->data))->value.type = NFA_EPSILON;
    ((NFA *)(li->data))->out2 = terminator->parent;
    li->data = ((NFA *)li->data)->parent;
    li = li->next;
  }

  List * sub_tree = NULL;
  list_set_iterator(tmp, 0);
  while((sub_tree = list_get_next(tmp)) != NULL) {
    list_transfer(start->value.branches, sub_tree);
    list_free(sub_tree, NULL);
  }

  list_free(tmp, NULL);

  terminator->parent = start;

  return terminator;
}


//
// FUNCTION: concatenate_nfa:
// INPUT:
//  - body -- type: NFA  *
// OUTPUT:
//  - finsih -- type: NFA *
// SYNOPSIS:
//  
NFA *
concatenate_nfa(NFA * prev, NFA * next)
{
  if(next == NULL) {
    return prev;
  }

  NFA * discard_node = NULL;
  NFA * tmp          = NULL;

  if(prev != NULL) {
    discard_node = next->parent;
    tmp = prev->parent;
    *prev = *(next->parent);
    next->parent = tmp;
    discard_node->parent = discard_node->out1 = discard_node->out2 = NULL;
    discard_node->value.literal = 0;
    discard_node->parent = discard_node;
    nfa_dealloc(discard_node->ctrl->alloc, discard_node);
  }

  return next;
}


void *
nfa_compare_equal(void * nfa1, void *nfa2)
{
  void * ret = NULL;
  if(nfa1 == NULL || nfa2 == NULL) {
    return ret;
  }

  // FIXME: This seems like it might become an issue
  if(nfa1 == nfa2) {
    // just return a non null
    ret = nfa1;
  }

  return ret;
}

void
free_nfa(NFACtrl ** ctrl)
{
  if(ctrl == NULL || *ctrl == NULL) {
    return;
  }

  nfa_free_alloc(&((*ctrl)->alloc));
  list_free((*ctrl)->free_range, (void *)free);
  free(*ctrl);
  *ctrl = NULL;
}
