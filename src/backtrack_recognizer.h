#ifndef NFA_SIM_H_
#define NFA_SIM_H_

#define MAX_BACKREF_COUNT      9
#define MATCH_BUCKET_SIZE   1024

#include "regex_parser.h"

typedef struct Match {
  const char * start;
  const char * end;
  const char * last_match_end;
} Match;


typedef struct LoopRecord {
  int count;
} LoopRecord;


typedef struct NFASim {
  struct NFASimCtrl * ctrl;
  int size; // size of this struct plus size of loop_record
  int status;
  int interval_count;
  int tracking_intervals;
  int tracking_backrefs;
  const char * input_ptr;
  NFA * ip;
  NFA * start_state;
  Scanner * scanner;
  int * loop_record_flags;
  Match match;
  Match backref_match[MAX_BACKREF_COUNT];
  LoopRecord loop_record[];
} NFASim;


typedef struct NFASimCtrl {
  int match_idx;
  int filename_len;
  int loop_record_cap;
  const char * filename;
  const char * buffer_start;
  const char * buffer_end;
  const char * last_interval_pos;
  NFA  * start_state;
  List * thread_pool;
  List * active_threads;
  Scanner * scanner;
  ctrl_flags * ctrl_flags;
  Match match;
  char matches[MATCH_BUCKET_SIZE];
  const char * active_threads_sp[];
} NFASimCtrl;


int  run_nfa(NFASim *);
int  get_states(NFASim *, NFA *, List *, int, int, unsigned int);
void free_nfa_sim(NFASimCtrl *);
void flush_matches(NFASimCtrl *);
void * free_match_string(void *);
NFASim * reset_nfa_sim(NFASimCtrl *, NFA *);
NFASimCtrl * new_nfa_sim(Parser *, Scanner *, ctrl_flags *);

#endif
