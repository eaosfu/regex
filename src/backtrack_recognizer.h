#ifndef NFA_SIM_H_
#define NFA_SIM_H_

#define THREAD_LIMIT          20
#define MAX_BACKREF_COUNT     20
#define MATCH_BUCKET_SIZE   1024
#define MAX_BACKTRACK_DEPTH 1024

#include "regex_parser.h"


typedef struct Match {
  struct NFASimCtrl * ctrl;
  const char * start;
  const char * end;
  const char * last_match_end;
} Match;


typedef struct BacktrackRecord {
  int count;
  const char * input;
  const char * match;
  NFA  * restart_point;;
} BacktrackRecord;


typedef struct LoopRecord {
  int count;
  char * last_match;
} LoopRecord;


typedef struct NFASim {
  struct NFASimCtrl * ctrl;
  int sp;
  int id;
  int tracking_intervals;
  int tracking_backrefs;
  int size;
  int tracked_loop_count; // may not be necessary anymore
  int status;
  const char * input_ptr;
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
  int next_thread_id;
  int match_idx;
  int loop_record_cap;
  Match match;
  List * match_pool;
  List * thread_pool;
  List * active_threads;
  ctrl_flags * ctrl_flags;
  char matches[MATCH_BUCKET_SIZE];
} NFASimCtrl;


int  run_nfa(NFASim *);
void free_nfa_sim(NFASim *);
void * free_match_string(void *);
void flush_matches(NFASimCtrl *);
void reset_nfa_sim(NFASim *, NFA *);
NFASim * new_nfa_sim(Parser *, Scanner *, ctrl_flags *);
int  get_states(NFASim *, NFA *, List *, int, int, unsigned int);

#endif
