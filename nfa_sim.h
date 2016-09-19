#ifndef NFA_SIM_H_
#define NFA_SIM_H_

typedef struct Match {
  char * buffer;
  int start;
  int end;
} Match;


typedef struct NFASim {
  enum {START, MATCHING} state;
  NFA * nfa;
  char * buffer;
  char * input_ptr;
  int match_start;
  List * state_set1;
  List * state_set2;
  List * matches;
} NFASim;


int match(NFA *, int);
int run_nfa(NFASim *);
int get_states(NFA *, List *);
NFASim * new_nfa_sim(NFA *, char *);
Match * new_matched_string(char *, int, int, List *);
static inline int is_literal_in_range(nfa_range, unsigned int);
void print_matches(List *);
void free_nfa_sim(NFASim *);
void reset_nfa_sim(NFASim *);
void nfa_sim_reset_buffer(NFASim *, char *);
void * free_match_string(void *);

#endif
