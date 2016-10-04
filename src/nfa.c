// TODO: implement nfa_pool
#include <stdlib.h>
#include <string.h>
#include "slist.h"
#include "nfa.h"
#include "misc.h"

#include <stdio.h>

NFA *
new_nfa(unsigned int type)
{
//printf("ALLOCATED NFA: ");
  // TOOD: check nfa_pool, if not empty pop an nfa from the pool
  //       set its type to 't' and it's parent, out1, out2 pointers to NULL and
  //       return that as the new nfa
  NFA * nfa = xmalloc(sizeof * nfa);
  nfa->value.type = type;
  nfa->parent = nfa->out1 = nfa->out2 = NULL;
//printf("ALLOCATED NEW NFA: 0x%x\n", nfa);
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

//printf("COLLATION STRING: '%s'\n", collation_string);

#define low_bound(i, j)  collations[(i)].ranges[(j)].low
#define high_bound(i, j) collations[(i)].ranges[(j)].high
#define NAMEQ(i) \
  (strncmp(collation_string, collations[(i)].name, coll_name_len))
//(strncmp(collation_string, collations[(i)].name, collations[(i)].name_len))

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
if(low == high && low == '.') {
//  printf("UPDATED RANGE NFA: low %d high %d\n", low, high);
}
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
new_range_nfa(int negate)
{
  NFA * start  = new_nfa(NFA_RANGE);
  NFA * accept = new_nfa(NFA_ACCEPTING);
  
  start->value.range = xmalloc(sizeof *(start->value.range));

  if(negate) {
    for(int i = 0; i < SIZE_OF_RANGE; ++i) {
      (*(start->value.range))[i] = 0xffffffff;
    }
  }

//printf("NEW EMPTY RANGE NFA\n");
//printf("NEW_RANGE: start: 0x%x, end: 0x%x\n", start, accept);
  accept->parent = start;

  start->out1 = start->out2 = accept;

   return accept;
}

/*
NFA *
new_range_nfa(unsigned int low, unsigned int high, int negate)
{
  NFA * start  = new_nfa(NFA_RANGE);
  NFA * accept = new_nfa(NFA_ACCEPTING);
  
  start->value.range = xmalloc(sizeof *(start->value.range));

  if(negate) {
    for(int i = 0; i < SIZE_OF_RANGE; ++i) {
      (*(start->value.range))[i] = 0xffffffff;
    }
  }

//printf("NEW RANGE NFA FROM %d TO %d\n", low, high);
  //update_range_nfa(low, high, start->value.range, negate);
  update_range_nfa(low, high, start, negate);

  accept->parent = start;

  start->out1 = start->out2 = accept;

   return accept;
}
*/

NFA *
new_anchor_nfa(unsigned int anchor)
{
  NFA * start = new_nfa(anchor);
  NFA * accept = new_nfa(NFA_ACCEPTING);

  start->out1 = start->out2 = accept;
  accept->parent = start;

  return accept;
}


NFA *
new_literal_nfa(unsigned int literal, unsigned int special_meaning)
{
  NFA * start;
  NFA * accept = new_nfa(NFA_ACCEPTING);

  if(special_meaning) {
//printf("SPECIAL MEANING: %d for char: %c\n", special_meaning, literal);
    start = new_nfa(special_meaning);
  }
  else {
    start = new_nfa(NFA_LITERAL);
  }
  
  start->value.literal = literal;
  start->out1 = start->out2 = accept;

  accept->parent = start;

//printf("NEW_LITERAL(%c:%d): start: 0x%x, end: 0x%x\n", literal, literal, start, accept);
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
  NFA * start  = new_nfa(NFA_SPLIT);
  NFA * accept = new_nfa(NFA_ACCEPTING);

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
  NFA * start  = new_nfa(NFA_SPLIT);
  NFA * accept = new_nfa(NFA_ACCEPTING);

  start->out1 = body->parent;
  start->out2 = accept;

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
  NFA * start  = new_nfa(NFA_EPSILON);
  NFA * accept = new_nfa(NFA_ACCEPTING);

  // there is no direct transition to the accepting state
  start->out1 = start->out2 = body->parent;

  body->value.type = NFA_SPLIT;
  body->out1 = body->parent;
  body->out2 = accept;

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
new_alternation_nfa(NFA * nfa1, NFA * nfa2)
{
////printf("BUILDING NEW ALTERNATION NFA\n");
  NFA * start  = new_nfa(NFA_SPLIT);
  NFA * accept = new_nfa(NFA_ACCEPTING);

  // set the accepting states of nfa1 and nfa2 to type EPSILON so we
  // don't confuse them for ACCEPTING states when we simulate the NFA
  nfa1->value.type = nfa2->value.type = NFA_EPSILON;
  nfa1->out1 = nfa1->out2 = nfa2->out1 = nfa2->out2 = accept;
  accept->parent = start;

  // nfa1->parent is the start state for nfa1; likewise for nfa2
  start->out1 = nfa1->parent;
  start->out2 = nfa2->parent;

//printf("DONE BUILDING NEW ALTERNATION NFA\n");
//printf("START: [ 0x%x ] ; END: [ 0x%x ]\n", accept->parent, accept);
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

  if(prev != NULL) {
    // If prev is not null then it's an Accepting state
    //prev->type = next->parent->type;
    prev->value = next->parent->value;
    prev->out1 = next->parent->out1;
    prev->out2 = next->parent->out2;
    NFA * discard_node = next->parent;
    next->parent = prev->parent;
//printf("CONCAT FREEING NODE\n");
//printf("FREEING NFA: 0x%x\n", discard_node);
//printf("CONCAT: 0x%x -> 0x%x -> 0x%x\n", prev->parent, prev->out2, next);
    discard_node->parent = discard_node->out1 = discard_node->out2 = NULL;
    //free_nfa(discard_node);
    free(discard_node);
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

  if(nfa1 == nfa2) {
    // just return a non null
    ret = nfa1;
  }

  return ret;
}

void
free_nfa_helper(NFA * n, List * l, List * seen_states)
{
  if(n == NULL) {
    return;
  }

  int i = 0;
  int already_seen = 0;

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
  int total_states = 0;
  // step 1 - load the set of next states;
  // step 2 - delete NFAs in set of current states
  // step 3 - move NFA's in set of next states into set of current states
  // repeat until all states have been deleted
//printf("nfa: 0x%x\n", nfa);
  g_states_added = 0;
  free_nfa_helper(nfa->parent, next_state_set, seen_states);
//printf("FREE NFA\n");
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
  for(int i = 0; i < seen_states->size; ++i) {
    del_nfa = (NFA *)list_get_at(seen_states, i);
    if(del_nfa->value.type == NFA_RANGE) {
      free(del_nfa->value.range);
    }
//printf("FREEING NFA: 0x%x\n", del_nfa);
    free(del_nfa);
  }

  list_free(cur_state_set, NULL);
  list_free(next_state_set, NULL);
  list_free(seen_states, NULL);
}