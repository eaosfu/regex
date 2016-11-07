#ifndef NFA_H_
#define NFA_H_
#include <limits.h>
#include "slist.h"

#define MAX_NFA_STATES 512
#define SIZE_OF_LOCALE 128
#define SIZE_OF_RANGE SIZE_OF_LOCALE/(sizeof(unsigned int) * CHAR_BIT)

#define NFA_ANY              0x0001
#define NFA_LITERAL          0x0002
#define NFA_RANGE            0x0004
#define NFA_SPLIT            0x0008
#define NFA_EPSILON          0x0010
#define NFA_ACCEPTING        0x0020
#define NFA_BOL_ANCHOR       0x0040
#define NFA_EOL_ANCHOR       0x0080
#define NFA_NGLITERAL        0x0100 // negated literal as in [^abc]
#define NFA_INTERVAL         0x0200 // negated literal as in [^abc]
#define NFA_BACKREFERENCE    0x0400 
#define NFA_CAPTUREGRP_END   0x0800 
#define NFA_CAPTUREGRP_BEGIN 0x1000 
#define NFA_MERGE_NODE       0x80000000


typedef unsigned int nfa_range[SIZE_OF_RANGE];

typedef struct NFACtrl {
  struct NFACtrl * ctrl_id;
  unsigned int next_seq_id;
  List * free_range;
  struct NFA * free_nfa;
  struct NFA * last_free_nfa;
} NFACtrl;

typedef struct NFA {
  struct NFACtrl * ctrl;
  unsigned int id;
  struct {
    unsigned int type;
    union {
      unsigned int literal;
      nfa_range * range;
      struct {
        unsigned int min_rep;
        unsigned int max_rep;
        unsigned int count;
      };
    };
  } value;
  struct NFA * parent;
  struct NFA * out1;
  struct NFA * out2;
} NFA;

NFACtrl * new_nfa_ctrl(void);
NFA * concatenate_nfa(NFA *, NFA *);
NFA * new_nfa(NFACtrl *, unsigned int);
NFA * new_alternation_nfa(NFA *, NFA *);
NFA * new_kleene_nfa(NFA *);
NFA * new_literal_nfa(NFACtrl *, unsigned int, unsigned int);
NFA * new_interval_nfa(NFA *, unsigned int, unsigned int);
NFA * new_posclosure_nfa(NFA *);
NFA * new_qmark_nfa(NFA *);
NFA * new_range_nfa(NFACtrl *, int);
NFA * new_backreference_nfa(NFACtrl *, unsigned int);


void free_nfa(NFA *);
void release_nfa(NFA *);
void update_range_nfa(unsigned int, unsigned int, NFA *, int);
void inject_capturegroup_markers(NFA *, NFA *, unsigned int);
int  update_range_w_collation(char *, int, NFA *, int);

#endif
