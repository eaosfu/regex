#ifndef NFA_SIM_H_
#define NFA_SIM_H_

#include <limits.h>
#define UINT_BITS (sizeof(unsigned int) * CHAR_BIT)
#define WORKING_SET_ID_BITS (MAX_NFA_STATES / UINT_BITS + \
                            ((MAX_NFA_STATES % UINT_BITS == 0) ? 0 : 1))

#include "regex_parser.h"

typedef struct Match {
  char * buffer;
  char * start;
  char * end;
} Match;

typedef struct NFASim {
  Scanner * scanner;
  Parser * parser;
  NFA * nfa;
  unsigned int working_set_ids[WORKING_SET_ID_BITS];  // bit set for each 'significant node id' present in state_set2
  List * state_set1;
  List * state_set2;
  List * tmp;            // this needs to be removed... need to fix list append
  List * matches;
  ctrl_flags * ctrl_flags;
} NFASim;


int run_nfa(NFASim *);
int get_states(NFASim *, NFA *, List *);
void new_matched_string(NFASim *, int, int);
void print_matches(List *);
void free_nfa_sim(NFASim *);
void reset_nfa_sim(NFASim *);
void * free_match_string(void *);
NFASim * new_nfa_sim(Parser *, Scanner *, ctrl_flags *);

#endif
