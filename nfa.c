// TODO: implement nfa_pool
#include <stdlib.h>

#include "nfa.h"
#include "misc.h"
#include <stdio.h>

NFA *
new_nfa(unsigned char type)
{
  // TOOD: check nfa_pool, if not empty pop an nfa from the pool
  //       set its type to 't' and it's parent, out1, out2 pointers to NULL and
  //       return that as the new nfa
  NFA * nfa = xmalloc(sizeof * nfa);
  nfa->value.type = type;
  nfa->parent = nfa->out1 = nfa->out2 = NULL;
}


void
update_range_nfa(unsigned int low, unsigned int high, nfa_range * range)
{
printf("Update range\n");
  for(int i = low; i <= high; ++i) {
    (*range)[(i / SIZE_OF_RANGE)] |= 0x01 << (i % 32);
  }
}


NFA *
new_range_nfa(unsigned int low, unsigned int high)
{
  NFA * start  = new_nfa(NFA_RANGE);
  NFA * accept = new_nfa(NFA_ACCEPTING);
  
  start->value.range = xmalloc(sizeof *(start->value.range));

printf("NEW RANGE NFA FROM %d TO %d\n", low, high);
  update_range_nfa(low, high, start->value.range);

  accept->parent = start;

  start->out1 = start->out2 = accept;

   return accept;
}


NFA *
new_literal_nfa(unsigned int literal)
{
  NFA * start = new_nfa(NFA_LITERAL);
  NFA * accept = new_nfa(NFA_ACCEPTING);
  
  start->value.literal = literal;
  start->out1 = start->out2 = accept;

  accept->parent = start;

printf("NEW_LITERAL(%c:%d): start: 0x%x, end: 0x%x\n", literal, literal, start, accept);
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
printf("BUILDING NEW ALTERNATION NFA\n");
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

printf("DONE BUILDING NEW ALTERNATION NFA\n");
printf("START: [ 0x%x ] ; END: [ 0x%x ]\n", accept->parent, accept);
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
    free(discard_node);
  }

  return next;
}
