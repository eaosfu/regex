#ifndef NFA_SIM_H_
#define NFA_SIM_H_

#include "regex_parser.h"

typedef struct Match {
  char * buffer;
  char * start;
  char * end;
} Match;


typedef struct NFASim {
  Parser * parser;
  NFA * nfa;
  int ctrl_flags;
  List * state_set1;
  List * state_set2;
  List * matches;
} NFASim;


int run_nfa(NFASim *);
int get_states(NFA *, List *);
void new_matched_string(NFASim *, int, int);
void print_matches(List *);
void free_nfa_sim(NFASim *);
void reset_nfa_sim(NFASim *);
void * free_match_string(void *);
NFASim * new_nfa_sim(Parser *);

#endif
