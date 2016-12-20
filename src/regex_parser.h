#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H
#include "nfa.h"
#include "token.h"
#include "stack.h"
#include "scanner.h"

#define CAPTURE_GROUP_MAX 9
#define MAX_REGEX_LENGTH  256

// FIXME: this needs to be moved out of here and out of recognizer.h
// and into a common header file!
#ifndef UINT_BITS
  #ifdef __linux__
    #if(__x86_64__ || __i386__)
    #define UINT_BITS (4 * CHAR_BIT)
    #endif
  #else
  #endif
#endif

#define MINIMUM_COMPLEX_REGEX_LENGTH 5


#if((((MAX_REGEX_LENGTH - 1)) % UINT_BITS) == 0)
  #define CGRP_MAP_SIZE                        \
    ((MAX_REGEX_LENGTH - 1)/UINT_BITS)
#else
  #define CGRP_MAP_SIZE                        \
    ((MAX_REGEX_LENGTH)/UINT_BITS + 1)
#endif

#define CGRP_MAP_CHUNK_BITS 2

#define CGRP_MAP_BLOCK_BITS UINT_BITS

#define CGRP_MAP_CHUNKS_IN_BLOCK \
  (CGRP_MAP_BLOCK_BITS/CGRP_MAP_CHUNK_BITS)

#define CGRP_MAP_BLOCK(i) \
  ((i) - 1)/CGRP_MAP_CHUNKS_IN_BLOCK

#define CGRP_MAP_CHUNK(i) \
  (((i) - 1) * 2)
  
// Return 1 if capture group under the influence of an
// interval expression and also contains loops
#define cgrp_is_complex(cgrp, i)                                    \
  (cgrp[CGRP_MAP_BLOCK((i))] & (0x001 << CGRP_MAP_CHUNK((i))))

// Return 1 if capture group i is backreferenced
#define cgrp_has_backref(cgrp, i)                                    \
  (cgrp[CGRP_MAP_BLOCK((i))] & (0x001 << (CGRP_MAP_CHUNK((i)) + 1)))


#define mark_closure_map_complex(cgrp, i)                              \
  ({do {                                                               \
    if((i) > 0) {                                                      \
      cgrp[CGRP_MAP_BLOCK(i)] |= (0x001 << CGRP_MAP_CHUNK((i)));       \
     }                                                                 \
  } while(0);})
  
  
#define mark_closure_map_backref(cgrp, i)                              \
  ({do {                                                               \
    if((i) > 0) {                                                      \
      cgrp[CGRP_MAP_BLOCK(i)] |= (0x001 << (CGRP_MAP_CHUNK((i)) + 1)); \
     }                                                                 \
  } while(0);})


#define INTERVAL(p)                                                    \
  (((p)->interval_list_sz)                                             \
  ? &((p)->interval_list[(p)->influencing_interval])                   \
  : 0)

#define PROVISIONED_INTERVAL(p)                                        \
  (((p)->interval_list_sz)                                             \
  ? ((p)->next_interval_id <= (p->interval_list_sz))                   \
    ? &((p)->interval_list[(p)->next_interval_id - 1])                 \
    : NULL                                                             \
  : NULL)

#define PARENT_INTERVAL(p)                                            \
  ((p->is_complex)                                                    \
  ? ((p)->next_interval_id <= (p->interval_list_sz))                  \
    ? (&((p)->interval_list[(p)->next_interval_id - 1]))              \
    : NULL                                                            \
  : NULL)


#define NFA_TRACK_PROGRESS(nfa) ((nfa)->value.type |= NFA_PROGRESS)

typedef struct IntervalRecord {
  NFA * node;
  unsigned int min_rep;
  unsigned int max_rep;
  unsigned int count;
} IntervalRecord;


typedef struct Parser {
  Token lookahead;
  Scanner    * scanner;
  Stack      * stack;
  Stack      * symbol_stack;
  Stack      * branch_stack;
  List       * loop_nfas;
  NFACtrl    * nfa_ctrl;
  ctrl_flags * ctrl_flags;

  int requires_backtracking;
  
  // Count open parens
  int paren_count;

  // counts how many branches need to be taken off the top of the branch_stack
  // and tied
  int tie_branches;

  // count the number of branches in the current nested sub-expression
  int subtree_branch_count;

  // what is the capture group id of the last capture-group seen before
  // entering the current branch
  int branch_id;

  int push_to_branch_stack;

  // If branch was purged... then the closing paren was consumed
  // this flag is set to avoid erroring out if a close-paren was
  // expected
  int ignore_missing_paren;

  // CGRP STUFF
  unsigned int cgrp_count;
  int root_cgrp;
  int current_cgrp;
  int in_new_cgrp;
  int in_alternation;
  int in_complex_cgrp;

  // INTERVAL STUFF
  int interval_list_sz;
  int in_cmplex_interval;
  unsigned int next_interval_id;
  int influencing_interval;
  NFA * interval_list;

  // TESTING:
  int alt_sz;

  // current tokens are 'complex'
  int is_complex;

  // Flag to indicate whether to use backtracking or not
  int use_backtracking;

  int cgrp_map[CGRP_MAP_SIZE];
} Parser;


Parser * init_parser(Scanner *, ctrl_flags *);
void parser_free(Parser *);
void parse_regex(Parser *);

#endif
