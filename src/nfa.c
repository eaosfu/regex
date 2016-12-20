// TODO: implement nfa_pool
#include <stdlib.h>
#include <string.h>
#include "stack.h"
#include "nfa.h"
#include "misc.h"

#include <stdio.h>


NFACtrl *
new_nfa_ctrl()
{
  NFACtrl * nfa_ctrl = xmalloc(sizeof * nfa_ctrl);
  nfa_ctrl->free_range = new_list();
  nfa_ctrl->ctrl_id = nfa_ctrl;
  nfa_ctrl->next_seq_id = 1;
  nfa_ctrl->next_interval_seq_id = 1;
  return nfa_ctrl;
}


static inline int
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
new_range_nfa(NFACtrl * ctrl, NFA * interval, int negate, unsigned int branch_id)
{
  NFA * start  = new_nfa(ctrl, NFA_RANGE);
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);
  
  mark_nfa(start);
 
  if(interval) {
    start->parent = interval;
    start->value.type |= NFA_IN_INTERVAL;
  }
  start->branch_id = branch_id;
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
new_lliteral_nfa(NFACtrl * ctrl, NFA * interval, char * src, 
   unsigned int len, unsigned int branch_id)
{
  NFA * start    = new_nfa(ctrl, NFA_EPSILON);
  NFA * lliteral = xmalloc(sizeof(*lliteral)+ len + 1);
  NFA * accept   = new_nfa(ctrl, NFA_ACCEPTING);

  start->out1 = start->out2 = lliteral;

  lliteral->ctrl = ctrl;
  mark_nfa(lliteral);
  lliteral->branch_id  = branch_id;
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
new_literal_nfa(NFACtrl * ctrl, NFA * interval, unsigned int literal, unsigned int type, unsigned int branch_id)
{
  NFA * start;
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);

  start = new_nfa(ctrl, type);

  start->branch_id = branch_id;

  mark_nfa(start);
  
  if(interval) {
    start->parent = interval;
//    start->value.type |= NFA_IN_INTERVAL;
  }

  start->value.literal = literal;
  start->out1 = start->out2 = accept;

  accept->parent = start;

  return accept;
}


NFA *
new_backreference_nfa(NFACtrl * ctrl, NFA * interval, unsigned int capture_group_id, unsigned int branch_id)
{
  NFA * start  = new_nfa(ctrl, NFA_BACKREFERENCE);
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);

  start->parent = interval;
  start->id = capture_group_id;
  start->branch_id = branch_id;
  start->out1 = start->out2 = accept;

  accept->parent = start;

  return accept;
}


NFA *
new_kleene_nfa(NFA * body)
{
  NFA * start  = new_nfa(body->ctrl, NFA_SPLIT);
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);

  mark_interval_nfa(start);

  start->value.literal = '?';
  start->greedy = 1;
  start->out1 = accept;
  start->out2 = body->parent;

  body->value.type = NFA_SPLIT;
  body->greedy = 1;
  body->value.literal = '*';
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
  
  
  mark_interval_nfa(start);

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

  // there is no direct transition to the accepting state
  start->out1 = start->out2 = body->parent;

  body->value.type = NFA_SPLIT;
  body->greedy = 1;
  mark_interval_nfa(body);
  body->value.literal = '+';
  body->out1 = body->parent;
  body->out2 = accept;

  accept->parent = start;

  return accept;
}

/*
NFA *
new_interval_nfa(NFA * body, unsigned int min, unsigned int max)
{
  if(body == 0) {
    // should never hit this condition
    return NULL;
  }

  NFA * start  = body;
  NFA * accept = new_nfa(body->ctrl, NFA_ACCEPTING);

  mark_nfa(start);
  start->value.type = NFA_INTERVAL;
  start->value.min_rep = min;
  start->value.max_rep = max;
  start->value.count = 0;

  start->out1 = start->out2 = accept;

  accept->parent = start->parent;

  return accept;
}
*/


// This is for intervals that only influence a single character
// i.e <expression>{min, Max} where <expression> is a single character.
// These can however be under the influence of another interval. If this
// is the case, the new interval's 'parent' pointer will point to the 
// influencing interval.
NFA *
new_interval_nfa(NFACtrl * ctrl, NFA * target, NFA * interval, unsigned int min, unsigned int max)
{
  NFA * start = new_nfa(ctrl, NFA_EPSILON);
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);
  NFA * new_interval = new_nfa(ctrl, NFA_INTERVAL);


  if(min == 0) {
    start->value.type = NFA_SPLIT;
    start->value.literal = '*';
    start->greedy = 1;
    start->out1 = target->parent;
    start->out2 = accept;
  }
  else {
    start->out1 = start->out2 = target->parent;
  }

  target->value.type = NFA_EPSILON;
  target->out2 = new_interval;

  mark_interval_nfa(new_interval);

  new_interval->value.min_rep = min;
  new_interval->value.max_rep = max;
  new_interval->value.count = 0;

  new_interval->parent = interval;

  new_interval->out1 = target->parent;

  if(target->parent->value.type == NFA_LITERAL
  || target->parent->value.type == NFA_LONG_LITERAL) {
//    target->parent->value.type |= NFA_IN_INTERVAL;
    target->parent->parent = new_interval;
  }

  new_interval->out2 = accept;


  accept->parent = start;

  return accept;
}


NFA *
fill_interval_nfa(NFACtrl * ctrl, NFA * target, NFA * interval, NFA * parent_interval, 
  unsigned int min, unsigned int max)
{
  NFA * start = new_nfa(ctrl, NFA_EPSILON);
  NFA * accept = new_nfa(ctrl, NFA_ACCEPTING);

  start->out1 = start->out2 = target->parent;

  target->value.type = NFA_EPSILON;
  target->out2 = target->out1 = interval;

  if(!interval) {
    fatal("Expected a provisioned interval\n");
  }


  interval->ctrl = target->ctrl;
  interval->value.type = NFA_INTERVAL;
  interval->value.min_rep = min;
  interval->value.max_rep = max;
  interval->value.count = 0;
  interval->parent = (interval == parent_interval) ? NULL : parent_interval;
  interval->out1 = target->parent;
  interval->out2 = accept;

  mark_interval_nfa(interval);

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


NFA *
nfa_tie_branches(NFA * target, List * branches_list, unsigned int num_branches)
{
#ifdef DEBUG_OR_TEST
  if(target == 0 || branches_list == 0 || num_branches < 2) {
    // should never hit this condiiton
    fatal("No branches in alternation\n");
  }
#else
#endif

  return new_alternation_nfa(target->ctrl, branches_list, num_branches, target);
}


/*
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
//  accept->value.type |= NFA_MERGE_NODE;

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
*/


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
    (*(NFACtrl **)nfa)->free_nfa = nfa->parent;
  }
  return;
}


void
free_nfa_helper(NFA * n, List * l, List * seen_states)
{
  if(n == NULL) {
    return;
  }

  if(!list_search(seen_states, n, nfa_compare_equal)) {
    list_push(seen_states, n);
    //list_append(seen_states, n);
    if(n->value.type & ~(NFA_SPLIT)) {
      list_push(l, n);
      ++g_states_added;
    }
    else {
      if((n->value.type & (NFA_SPLIT))) {
        // n->out1 is not a loop
        free_nfa_helper(n->out1, l, seen_states);
      }
      if(n->out2) {
        free_nfa_helper(n->out2, l, seen_states);
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
  NFACtrl * ctrl = (*(NFACtrl **)nfa)->ctrl_id;
  int total_states = 0;
  // step 1 - load the set of next states;
  // step 2 - delete NFAs in set of current states
  // step 3 - move NFA's in set of next states into set of current states
  // repeat until all states have been deleted
//printf("nfa: 0x%x\n", nfa);
  g_states_added = 0;
  free_nfa_helper(nfa->parent, next_state_set, seen_states);
//printf("FREE NFA\n");
  nfa->out2 = (*(NFACtrl **)nfa)->free_nfa;
  while(g_states_added > 0) {
//printf("STATES ADDED: %d\n", g_states_added);
    total_states += g_states_added;
    g_states_added = 0;
    list_swap(cur_state_set, next_state_set);
    list_clear(next_state_set);
    while((nfa = list_shift(cur_state_set))) {
    //for(int i = 0; i < cur_state_set->size; ++i) {
      //nfa = list_get_at(cur_state_set, i);
      if((nfa = nfa->out2)){
        free_nfa_helper(nfa, next_state_set, seen_states);
      }
    }
  }
 
  NFA * del_nfa = NULL;
//  for(int i = 0; i < seen_states->size; ++i) {
  while((del_nfa = list_shift(seen_states))) {
    //del_nfa = list_get_at(seen_states, i);
    if(del_nfa->value.type == NFA_RANGE) {
      free(del_nfa->value.range);
    }
//printf("FREEING NFA: 0x%x\n", del_nfa);
    free(del_nfa);
  }

  list_free(&cur_state_set, NULL);
  list_free(&next_state_set, NULL);
  list_free(&seen_states, NULL);
  list_free(&(ctrl->free_range), (void *)free);
}
