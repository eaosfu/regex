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


#define THREAD_LIMIT          20
#define MAX_BACKREF_COUNT     20
#define MATCH_BUCKET_SIZE   1024
#define MAX_BACKTRACK_DEPTH 1024

#include "regex_parser.h"


typedef struct Match {
  struct NFASimCtrl * ctrl;
  char * start;
  char * end;
} Match;


typedef struct BacktrackRecord {
  int next_branch;
  int count;
  char * input;
  char * match;
  NFA  * restart_point;;
} BacktrackRecord;


typedef struct LoopRecord {
  int count;
  char * last_match;
} LoopRecord;


typedef struct NFASim {
  struct NFASimCtrl * ctrl;
  int sp;
  int size;
  int tracked_loop_count;
  char * input_ptr;
  NFA * ip;
  NFA * start_state;
  Scanner * scanner;
  Match match;
  struct NFASim * next_thread;
  struct NFASim * prev_thread;
  BacktrackRecord backtrack_stack[MAX_BACKTRACK_DEPTH];
  Match backref_match[MAX_BACKREF_COUNT];
  LoopRecord loop_record[];
} NFASim;


typedef struct NFASimCtrl {
  int match_idx;
  Match match;
  List * match_pool;
  List * thread_pool;
  ctrl_flags * ctrl_flags;
  char matches[MATCH_BUCKET_SIZE];
  int num_cacned_states;
  NFASim cached_states[];
} NFASimCtrl;


int  run_nfa(NFASim *);
int  get_states(NFASim *, NFA *, List *, int, int, unsigned int);
void new_matched_string(NFASim *, int, int);
void free_nfa_sim(NFASim *);
void * free_match_string(void *);
NFASim * new_nfa_sim(Parser *, Scanner *, ctrl_flags *);

#endif
