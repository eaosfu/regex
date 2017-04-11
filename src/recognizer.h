#ifndef NFA_SIM_H_
#define NFA_SIM_H_

#include "regex_parser.h"
#include "mpat.h"

#include <stdio.h>

// this needs to be >= 3
//   -- 1 for the match
//   -- 1 for '\n'
//   -- 1 for '\0'
#ifdef MATCH_BUFFER_SIZE
  #if MATCH_BUFFER_SIZE < 3
    #define MATCH_BUFFER_SIZE      3
  #endif
#else
  #define MATCH_BUFFER_SIZE      BUFSIZ
#endif

typedef struct Match {
  const char * start;
  const char * end;
  const char * last_match_end;
} Match;


typedef struct LoopRecord {
  int count;
} LoopRecord;


typedef struct SimPoolList {
  struct NFASim * head;
  struct NFASim * tail;
} SimPoolList;


typedef struct NFASim {
  struct NFASimCtrl * ctrl;
  const char * input_ptr;
  const char * bref_ptr;
  NFA * ip;
  Scanner * scanner;
  Match match;
  int status;
  int tracking_intervals;
  int tracking_backrefs;
  int interval_count;
  struct NFASim * next;
  Match backref_match[CGRP_MAX];
  LoopRecord loop_record[];
} NFASim;


typedef struct NFASimCtrl {
  int match_idx;
  int filename_len;
  int loop_record_cap;
  int size; // size of this struct + size of loop_record
  const char * cur_pos;
  const char * filename;
  const char * buffer_start;
  const char * buffer_end;
  const char * last_interval_pos;
  NFA  * start_state;
  SimPoolList thread_pool;
  SimPoolList active_threads;
  SimPoolList next_threads;
  Scanner * scanner;
  MPatObj * mpat_obj;
  ctrl_flags * ctrl_flags;
  Match match;
  char matches[MATCH_BUFFER_SIZE];
  const char * active_threads_sp[];
} NFASimCtrl;


int  run_nfa(NFASimCtrl *);
int  get_states(NFASim *, NFA *, List *, int, int, unsigned int);
void free_nfa_sim(NFASimCtrl *);
void flush_matches(NFASimCtrl *);
void * free_match_string(void *);
void  reset_nfa_sim(NFASimCtrl *);
NFASimCtrl * new_nfa_sim(Parser *, Scanner *, ctrl_flags *);

#endif
