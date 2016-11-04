// TODO: implement nfa_pool
#include <stdlib.h>
#include <string.h>
#include "slist.h"
#include "nfa.h"
#include "misc.h"

#include <stdio.h>


NFACtrl *
new_nfa_ctrl()
{
  NFACtrl * nfa_ctrl = xmalloc(sizeof * nfa_ctrl);
  nfa_ctrl->ctrl_id = nfa_ctrl;
  nfa_ctrl->next_seq_id = 1;
  return nfa_ctrl;
}


static inline int
mark_nfa(NFA * nfa)
{
  if(nfa->id == 0) {
    if(nfa->ctrl->next_seq_id + 1 > MAX_NFA_STATES) {
      fatal("Too many states. Maximum is 512\n");
    }
    nfa->id = nfa->ctrl->next_seq_id;
    ++(nfa->ctrl->next_seq_id);
  }
}


NFA *
new_nfa(NFACtrl * ctrl, unsigned int type)
{
  NFA * nfa = NULL;
  if(ctrl->free_nfa) {
    nfa = ctrl->free_nfa;
    if(ctrl->free_nfa == ctrl->last_free_nfa) {
      ctrl->free_nfa = ctrl->last_free_nfa = NULL;
    }
    else {
      if(nfa->value.literal == '|') {
        ctrl->last_free_nfa->out2 = nfa->out1;
        ctrl->last_free_nfa = nfa->out1;
      }
      ctrl->free_nfa = nfa->out2;
    }
  }
  else {
    nfa = xmalloc(sizeof * nfa);
  }
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

  typedef struct {
    int low;
    int high;
  } coll_ranges;

  typedef struct { 
    const char * name;
    int name_len;
    int range_num;
    coll_ranges ranges[4];
  } named_collations;

  named_collations alnum  = {"alnum", 5, 3, {{65,90}, {97,122}, {48,57}}}; // [a-zA-Z0-9]
  named_collations alpha  = {"alpha", 5, 2, {{65,90}, {97,122}}}; // [a-zA-Z]
  named_collations blank  = {"blank", 5, 2, {{32,32}, {11,11}}};  // [ \t]
  named_collations cntrl  = {"cntrl", 5, 2, {{0,31,}, {127,127}}};   
  named_collations digit  = {"digit", 5, 1, {{48,57}}};  // [0-9]
  named_collations graph  = {"graph", 5, 1, {{33,126}}}; // [^[:cntrl:]]
  named_collations lower  = {"lower", 5, 1, {{97,122}}}; // [a-z]
  named_collations print  = {"print", 5, 1, {{32,126}}}; // [[:graph:] ]
  named_collations punct  = {"punct", 5, 4, {{33,47}, {58,64}, {91,96}, {123,126}}};
  named_collations space  = {"space", 5, 2, {{9,13}, {2,32}}}; // [ \t\v\f\c\r]
  named_collations upper  = {"upper", 5, 1, {{65,90}}}; // [A-Z]
  named_collations xdigit = {"xdigit",6, 2, {{65,78}, {97,102}}}; // [0-9A-Fa-f]


  int ncoll = 12;
  named_collations collations[] = {
    punct, alnum, 
    cntrl, alpha,
    blank, space, 
    xdigit, graph,
    digit, print,
    lower, upper
  };

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
#define index(i)  ((i) / 32)
#define offset(i) ((i) % 32)
#define set_bit(r, i)   (*(r))[index(i)] |= 0x01 << offset(i)
#define unset_bit(r, i) (*(r))[index(i)] &= (0xFFFFFFFF ^ (0x01 << offset(i)))
  if(negate) {
    for(int i = low; i <= high; ++i) {
      unset_bit(range_nfa->value.range, i);
    }
  }
  else {
    for(int i = low; i <= high; ++i) {
      set_bit(range_nfa->value.range, i);
    }
  }
#undef set_bit
#undef unset_bit
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
      (*(start->value.range))[i] = 0xffffffff;
    }
  }

  accept->parent = start;

  start->out1 = start->out2 = accept;

   return accept;
}


NFA *
new_literal_nfa(NFACtrl * ctrl, unsigned int literal, unsigned int special_meaning)
{
  NFA * start;
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);

  if(special_meaning) {
    start = new_nfa(ctrl, special_meaning);
  }
  else {
    start = new_nfa(ctrl, NFA_LITERAL);
  }

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


//
// FUNCTION: new_kleene_nfa:
// INPUT:
//  - body -- type: NFA  *
// OUTPUT:
//  - finsih -- type: NFA *
// SYNOPSIS:
//  Creates tow new NFA nodes, start and accept. The start has two transitions,
//  the first to the body node and the second to the accept node. The body node
//  (which is realy just an accept node from a previous nfa... see comment in 
//  concatenate_nfa for further details on this) is converted to a SPLIT node
//  with out1 looping back to body->parent (i.e. body's start node) and out2
//  transitioning to the new accept state of the closure.
//
//  NOTE: The accept node's parent is set to the start node so that the nfa
//        returned appears as a single node.
//  
NFA *
new_kleene_nfa(NFA * body)
{
  NFA * start  = new_nfa(body->ctrl, NFA_SPLIT);
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);

  start->value.literal = '*';
  start->out1 = body->parent;
  start->out2 = accept;

  body->value.type = NFA_SPLIT;
  body->out1 = body->parent;
  body->out2 = accept;

  accept->parent = start;

  return accept;
}


NFA *
new_qmark_nfa(NFA * body)
{
  NFA * start  = new_nfa(body->ctrl, NFA_SPLIT);
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);

  start->out1 = body->parent;
  start->out2 = accept;
  start->value.literal = '?';

  body->value.type = NFA_EPSILON;
  body->out1 = accept;
  body->out2 = accept;

  accept->parent = start;

  return accept;
}


//
// FUNCTION: new_posclosure_nfa:
// INPUT:
//  - body -- type: NFA  *
// OUTPUT:
//  - finsih -- type: NFA *
// SYNOPSIS:
//  
NFA *
new_posclosure_nfa(NFA * body)
{
  NFA * start  = new_nfa(body->ctrl, NFA_EPSILON);
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);

  start->value.literal = '+';
  start->out1 = start->out2 = body->parent;

  body->value.type = NFA_SPLIT;
  body->out1 = body->parent;
  body->out2 = accept;

  accept->parent = start;

  return accept;
}


NFA *
new_interval_nfa(NFA * body, unsigned int min, unsigned int max)
{
  if(body == 0) {
    // should never hit this condition
    return NULL;
  }

  NFA * start  = body;
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);

  start->value.type = NFA_INTERVAL;
  start->value.min_rep = min;
  start->value.max_rep = max;
  start->value.count = 0;

  start->out1 = start->out2 = accept;

  accept->parent = start->parent;

  return accept;
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
new_alternation_nfa(NFA * nfa1, NFA * nfa2)
{

  if(nfa1 == NULL) {
    return nfa2;
  }
  if(nfa2 == NULL) {
    return nfa1;
  }

  if(nfa1->ctrl != nfa2->ctrl) {
    fatal("Alternation between different nfa families not allowed\n");
  }
  NFA * start  = new_nfa(nfa1->ctrl, NFA_SPLIT);
  NFA * accept = new_nfa(nfa1->ctrl, NFA_ACCEPTING);
  accept->value.type |= NFA_MERGE_NODE;

  // set the accepting states of nfa1 and nfa2 to type EPSILON so we
  // don't confuse them for ACCEPTING states when we simulate the NFA
  nfa1->value.type = nfa2->value.type = NFA_EPSILON;
  nfa1->out1 = nfa1->out2 = nfa2->out1 = nfa2->out2 = accept;
  accept->parent = start;

  // nfa1->parent is the start state for nfa1; likewise for nfa2
  start->value.literal = '|';
  start->out1 = nfa1->parent;
  start->out2 = nfa2->parent;

  return accept;
}


// FIXME not doing this properly, so getting duplicates!!!
//
// FUNCTION: concatenate_nfa:
// INPUT:
//  - body -- type: NFA  *
// OUTPUT:
//  - finsih -- type: NFA *
// SYNOPSIS:
//  
// TOOD: IMPLEMENT A MORE EFFICIENT USE OF MEMEORY MANAGEMENT
//       SO WE DON'T HAVE TO CALL FREE.
NFA *
concatenate_nfa(NFA * prev, NFA * next)
{
  if(next == NULL) {
    return prev;
  }

  NFA * discard_node = NULL;
  NFA * tmp          = NULL;
  unsigned int old_type = 0;

  if(prev != NULL) {
    discard_node = next->parent;
    tmp = prev->parent;
    old_type = prev->value.type;
    *prev = *(next->parent);
    prev->value.type |= (old_type & NFA_MERGE_NODE) ? old_type : 0;
    next->parent = tmp;
    discard_node->parent = discard_node->out1 = discard_node->out2 = NULL;
    discard_node->value.literal = 0;
    discard_node->parent = discard_node;
    release_nfa(discard_node);
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
release_nfa(NFA * nfa)
{
  if(nfa) {
    if((*(NFACtrl **)nfa)->free_nfa) {
      nfa->out2 = (*(NFACtrl **)nfa)->free_nfa;
    }
    else {
      nfa->out1 = nfa->out2 = NULL;
      (*(NFACtrl **)nfa)->last_free_nfa = nfa;
    }
    (*(NFACtrl **)nfa)->free_nfa = nfa->parent;
  }
  return;
}


unsigned int nodes_freed = 0;
void
free_alternation_nfa(NFA * head, NFA * parent, NFA * accept)
{
  NFA * target = parent->out1;
  NFA * tmp;
  while(target && !(target->value.type & NFA_MERGE_NODE)) {
    switch(target->value.literal) {
      case '|': {
        free_alternation_nfa(head, target, accept);
        tmp = target->out2;
      } break;
      case '+':
      case '?':
      case '*': {
        tmp = target->out1;
      } break;
      default: {
        tmp = target->out2;
      }
    }
    tmp = target->out2;
    free(target);
    ++nodes_freed;
    target = tmp;
  }
  return;
}


void
free_nfa(NFA * nfa)
{
  if(nfa == NULL) {
    return;
  }

  NFA * tmp = NULL;
  NFA * accept = nfa;
  NFA * head = nfa->parent;
  NFA * current = nfa->parent;
  if((*(NFACtrl **)nfa)->free_nfa) {
    nfa->out2 = (*(NFACtrl **)nfa)->free_nfa;
    (*(NFACtrl **)nfa)->last_free_nfa->out2 = NULL;
  }
  while(current) {
    switch(current->value.literal) {
      case '|': {
        free_alternation_nfa(head, current, accept);
        tmp = current->out2;
      } break;
      case '+':
      case '?': 
      case '*': {
        tmp = current->out1;
      } break;
      default: {
        tmp = current->out2;
      }
    }
    free(current);
    current = tmp;
    ++nodes_freed;
  }

  return;
}
