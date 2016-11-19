#ifndef NFA_SIM_H_
#define NFA_SIM_H_

#include <limits.h>

#ifndef UINT_BITS
  #ifdef __linux__
    #ifdef __x86_64__
      #define UINT_BITS (4 * CHAR_BIT)
    #endif
  #endif
#endif

#define WORKING_SET_ID_BITS (MAX_NFA_STATES / UINT_BITS + \
                            ((MAX_NFA_STATES % UINT_BITS == 0) ? 0 : 1))

#include "regex_parser.h"

typedef struct Match {
  char * buffer;
  char * start;
  char * end;
  unsigned int stream_id;
//  int iteration;
} Match;

typedef struct NFASim {
  Scanner * scanner;
  Parser * parser;
  NFA * nfa;
  List * state_set1;
  List * state_set2;
  List * matches;
  ctrl_flags * ctrl_flags;
  unsigned int * visited_backref_ids;
  // bit set for each 'significant node id' present in state_set2
  unsigned int stateset2_ids[WORKING_SET_ID_BITS];
  struct Match backref_matches[CAPTURE_GROUP_MAX];
} NFASim;


int run_nfa(NFASim *);
int get_states(NFASim *, NFA *, List *, int, int);
void new_matched_string(NFASim *, int, int);
void print_matches(List *);
void free_nfa_sim(NFASim *);
void reset_nfa_sim(NFASim *);
void * free_match_string(void *);
NFASim * new_nfa_sim(Parser *, Scanner *, ctrl_flags *);

#endif
