#include <stdlib.h>
#include <string.h>
#include "stack.h"
#include "nfa.h"
#include "misc.h"

#include <stdio.h>


int
get_next_seq_id(NFACtrl *ctrl)
{
  if(ctrl) {
    return ctrl->next_seq_id;
  }
  return 0;
}

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
    if(nfa->ctrl->next_interval_seq_id + 1 > MAX_NFA_STATES) {
      fatal("Too many states. Maximum is 512\n");
    }
    nfa->id = nfa->ctrl->next_interval_seq_id;
    ++(nfa->ctrl->next_interval_seq_id);
  }
}


void
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
    if(nfa->value.type == NFA_RANGE) {
      list_push((*(NFACtrl**)nfa)->free_range, nfa->value.range);
      nfa->value.range = 0;
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
  if(negate) {
    for(int i = low; i <= high; ++i) {
      clear_bit_array(*(range_nfa->value.range), RANGE_BITVEC_WIDTH, i);
    }
  }
  else {
    for(int i = low; i <= high; ++i) {
      set_bit_array(*(range_nfa->value.range), RANGE_BITVEC_WIDTH, i);
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
      (*(start->value.range))[i] = 0xffffffff;
    }
  }

  accept->parent = start;

  start->out1 = start->out2 = accept;

   return accept;
}


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

  //mark_interval_nfa(start);
start->id = body->parent->id;
  start->value.literal = '?';
  start->greedy = 1;
  start->out1 = accept;
  start->out2 = body->parent;

  body->value.type = NFA_SPLIT;
  body->greedy = 1;
  body->value.literal = '*';
//  body->out1 = body->parent;
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
//  NFA * start  = new_nfa(body->ctrl, NFA_QMARK);
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);
  
  
  //mark_interval_nfa(start);
start->id = body->parent->id;
  start->greedy = 1;
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
  body->greedy = 1;
  //mark_interval_nfa(body);
  body->value.literal = '+';
//  body->out1 = body->parent;
  // tighten loop

  NFA * target = body->parent;
  while((target->value.type == NFA_EPSILON) && (target = target->out2));
  body->out1 = target;

  body->out2 = accept;
  accept->parent = start;

  return accept;
}


// This is for intervals that only influence a single character
// i.e <expression>{min, Max} where <expression> is a single character.
// These can however be under the influence of another interval. If this
// is the case, the new interval's 'parent' pointer will point to the 
// influencing interval.
NFA *
new_interval_nfa(NFA * target, unsigned int min, unsigned int max)
{
  NFA * start = new_nfa(target->ctrl, NFA_EPSILON);
  NFA * accept = new_nfa(target->ctrl, NFA_ACCEPTING);
  NFA * new_interval = new_nfa(target->ctrl, NFA_INTERVAL);


  if(min == 0) {
    start->value.type = NFA_SPLIT;
    start->value.literal = '?';
    start->greedy = 1;
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
  // tighten loop
  while((tmp->value.type == NFA_EPSILON) && (tmp = tmp->out2));
  new_interval->out1 = tmp;
// new_interval->out1 = target->parent;
  new_interval->out2 = accept;


  accept->parent = start;

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
new_alternation_nfa(NFACtrl * ctrl, List * branches_list, unsigned int num_branches, NFA * terminator)
{
#ifdef DEBUG_OR_TEST
  if(ctrl == 0 || branches_list == 0) {
    // should never hit this condiiton
    fatal("No branches in alternation\n");
  }
#else
#endif

  if(num_branches < 2) {
    return pop(branches_list);
  }

  NFA * start  = new_nfa(ctrl, NFA_TREE);
  if(terminator == NULL) {
    terminator = new_nfa(ctrl, NFA_ACCEPTING);
    terminator->parent = terminator;
  }

  start->value.branches = list_chop(branches_list, num_branches);

  ListItem * li = start->value.branches->head;

  for(int i = 0; i < num_branches; ++i) {
    ((NFA *)li->data)->value.type = NFA_EPSILON;
    ((NFA *)li->data)->out2 = terminator->parent;
    li->data = ((NFA *)li->data)->parent;
    li = li->next;
  }

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
    release_nfa(discard_node);
  }

  return next;
}



static int g_states_added = 0;

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
    else { // first 'free_nfa'
      nfa->out1 = nfa->out2 = NULL;
      (*(NFACtrl **)nfa)->last_free_nfa = nfa;
    }
    nfa->value.type = NFA_LITERAL;
    (*(NFACtrl **)nfa)->free_nfa = nfa->parent;
  }
  return;
}


void *
free_nfa_wrapper(void * arg)
{
  free(arg);
  return NULL;
}


void
free_nfa_helper(NFA * n, List * l, List * seen_states)
{
  if(n == NULL) {
    return;
  }

  if(!list_search(seen_states, n, nfa_compare_equal)) {
    list_push(seen_states, n);
    n->value.type &= ~NFA_PROGRESS;
    if(n->value.type & ~(NFA_SPLIT|NFA_TREE)) {
      list_push(l, n);
      ++g_states_added;
    }
    else {
      if(n->value.type & NFA_TREE) {
        int branch_count = list_size(n->value.branches);
        for(int i = 0; i <= branch_count; ++i) {
          free_nfa_helper(list_shift(n->value.branches), l, seen_states);
        }
        //list_free(&(n->value.branches), NULL);
        list_free((n->value.branches), NULL);
      }
      else {
        free_nfa_helper(n->out1, l, seen_states);
        if(n->out2) {
          free_nfa_helper(n->out2, l, seen_states);
        }
      }
    }
  }
}


// walk the NFA collecting every node in the graph
// mark a node as 'special' if its type is NOT one of:
//   -- NFA_LITERAL    # matches a literal
//   -- NFA_NGLITERAL  # negates matching a literal
// all other nodes encountered are classified as 'simple'
void
free_nfa(NFA * nfa)
{
  if(nfa == NULL) {
    return;
  }

  List * cur_state_set  = new_list(); 
  List * next_state_set = new_list();
  List * seen_states    = new_list();
  NFACtrl * ctrl = nfa->ctrl;
  int total_states = 0;
  // step 1 - load the set of next states;
  // step 2 - delete NFAs in set of current states
  // step 3 - move NFA's in set of next states into set of current states
  // repeat until all states have been deleted
  g_states_added = 0;
  free_nfa_helper(nfa->parent, next_state_set, seen_states);
  nfa->out2 = (*(NFACtrl **)nfa)->free_nfa;
  while(g_states_added > 0) {
    total_states += g_states_added;
    g_states_added = 0;
    list_swap(cur_state_set, next_state_set);
    list_clear(next_state_set);
    while((nfa = list_shift(cur_state_set))) {
      if((nfa = nfa->out2)){
        free_nfa_helper(nfa, next_state_set, seen_states);
      }
    }
  }
 
  NFA * del_nfa = NULL;
  while((del_nfa = list_shift(seen_states))) {
    if(del_nfa->value.type & NFA_RANGE) {
      free(del_nfa->value.range);
    }
    list_free_items(&(del_nfa->reachable), NULL);

    free(del_nfa);
  }
  list_free(cur_state_set, NULL);
  list_free(next_state_set, NULL);
  list_free(seen_states, NULL);
  list_free((ctrl->free_range), &free_nfa_wrapper);
}
