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


#if((((MAX_REGEX_LENGTH - 1)) % UINT_BITS) == 0)
  #define CGRP_MAP_SIZE                        \
    ((MAX_REGEX_LENGTH - 1)/UINT_BITS)
#else
  #define CGRP_MAP_SIZE                        \
    ((MAX_REGEX_LENGTH)/UINT_BITS + 1)
#endif

#define MAX_CGRP (MAX_REGEX_LENGTH/2 - 1)

#define CGRP_MAP_CHUNK_BITS 2

#define CGRP_MAP_BLOCK_BITS UINT_BITS

#define CGRP_MAP_CHUNKS_IN_BLOCK \
  (CGRP_MAP_BLOCK_BITS/CGRP_MAP_CHUNK_BITS)

#define CGRP_MAP_BLOCK(i) \
  ((i) - 1)/CGRP_MAP_CHUNKS_IN_BLOCK

#define CGRP_MAP_CHUNK(i) \
  (((i) - 1) * 2)
 
// Return 1 if capture group i is backreferenced
#define cgrp_has_backref(cgrp, i) \
  (cgrp[CGRP_MAP_BLOCK((i))] & (0x001 << (CGRP_MAP_CHUNK((i)) + 1)))

  
#define mark_closure_map_backref(cgrp, i)                              \
  ({do {                                                               \
    if((i) > 0) {                                                      \
      cgrp[CGRP_MAP_BLOCK(i)] |= (0x001 << (CGRP_MAP_CHUNK((i)) + 1)); \
     }                                                                 \
  } while(0);})


#define NFA_TRACK_PROGRESS(nfa) ((nfa)->value.type |= NFA_PROGRESS)


typedef struct IntervalRecord {
  NFA * node;
  int min_rep;
  int max_rep;
  int count;
} IntervalRecord;

typedef struct CaptureGrpRecord {
  int next_id;
  char * end;
} CaptureGrpRecord;


typedef struct Parser {
  Token lookahead;
  Scanner    * scanner;
  Stack      * symbol_stack;
  Stack      * branch_stack;
  List       * loop_nfas;
  NFA        * prev_interval;
  NFA        * prev_interval_head;
  NFACtrl    * nfa_ctrl;
  ctrl_flags * ctrl_flags;

  // Count open parens
  int paren_count;

  // count the number of branches in the current nested sub-expression
  int subtree_branch_count;

  // Capture-Group Stuff
  int cgrp_count;
  int current_cgrp;
  int in_new_cgrp;
  int paren_idx;
  int paren_stack[MAX_CGRP];

  // Alternation Stuff
  int in_alternation;
  int tree_count;
  int branch;
  int lowest_id_on_branch;

  // Interval Stuff
  int interval_count;

  int total_nfa_ids;

  CaptureGrpRecord capgrp_record[CAPTURE_GROUP_MAX];
  int cgrp_map[CGRP_MAP_SIZE];
} Parser;


Parser * init_parser(Scanner *, ctrl_flags *);
void parser_free(Parser *);
int parse_regex(Parser *);

#endif
