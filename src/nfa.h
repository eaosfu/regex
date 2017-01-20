#ifndef NFA_H_
#define NFA_H_
#include <limits.h>
#include "bits.h"
#include "slist.h"

#define NFA_ANY              0x00001
#define NFA_LITERAL          0x00002
#define NFA_RANGE            0x00004
#define NFA_SPLIT            0x00008
#define NFA_EPSILON          0x00010
#define NFA_ACCEPTING        0x00020
#define NFA_BOL_ANCHOR       0x00040
#define NFA_EOL_ANCHOR       0x00080
#define NFA_NGLITERAL        0x00100 // negated literal as in [^abc]
#define NFA_INTERVAL         0x00200 // negated literal as in [^abc]
#define NFA_BACKREFERENCE    0x00400 
#define NFA_CAPTUREGRP_END   0x00800 
#define NFA_CAPTUREGRP_BEGIN 0x01000 
#define NFA_TREE             0x02000 
#define NFA_IN_INTERVAL      0x04000
#define NFA_LONG_LITERAL     0x10000
#define NFA_PROGRESS         0x20000


#define MAX_NFA_STATES 512
#define SIZE_OF_LOCALE 128

#define RANGE_BITVEC_WIDTH REGULAR_BITVEC_WIDTH
#define SIZE_OF_RANGE (SIZE_OF_LOCALE/RANGE_BITVEC_WIDTH)


typedef unsigned int nfa_range[SIZE_OF_RANGE];

typedef struct NFACtrl {
  struct NFACtrl * ctrl_id;
  unsigned int next_seq_id;
  unsigned int next_interval_seq_id;
  List * free_range;
  struct NFA * free_nfa;
  struct NFA * last_free_nfa;
} NFACtrl;

typedef struct NFA {
  struct NFACtrl * ctrl;
  struct NFA * parent;
  struct NFA * out1;
  struct NFA * out2;
  int greedy;
  unsigned int id;
  struct {
    unsigned int type;
    union {
      struct {
        unsigned int min_rep;
        unsigned int max_rep;
        unsigned int count;
      };
      nfa_range * range;
      List * branches;
      struct {
        unsigned int len;
        unsigned int idx;
        union {
          unsigned int literal;
          char * lliteral;
        };
      };
    };
  } value;
} NFA;

NFACtrl * new_nfa_ctrl(void);
NFA * concatenate_nfa(NFA *, NFA *);
NFA * new_nfa(NFACtrl *, unsigned int);
//NFA * new_simple_alternation_nfa(NFA *, NFA *);
NFA * new_alternation_nfa(NFACtrl *, List *, unsigned int, NFA *);
NFA * new_kleene_nfa(NFA *);
//NFA * new_literal_nfa(NFACtrl *, NFA *, unsigned int, unsigned int, unsigned int);
NFA * new_literal_nfa(NFACtrl *, unsigned int, unsigned int);
//NFA * new_lliteral_nfa(NFACtrl *, NFA *, char *, unsigned int, unsigned int);
NFA * new_lliteral_nfa(NFACtrl *, char *, unsigned int);
//NFA * new_long_literal_nfa(NFACtrl *, char *, unsigned int);
NFA * new_interval_nfa(NFA *, unsigned int, unsigned int);
//NFA * new_interval_nfa(NFACtrl *, NFA *, NFA *, unsigned int, unsigned int);
//NFA * new_interval_nfa(NFACtrl *, NFA *, unsigned int, unsigned int);
NFA * new_posclosure_nfa(NFA *);
NFA * new_qmark_nfa(NFA *);
NFA * new_range_nfa(NFACtrl *, NFA *, int, unsigned int);
NFA * new_backreference_nfa(NFACtrl *, NFA *, unsigned int, unsigned int);
NFA * finalize_nfa(NFA *);

NFA * nfa_tie_branches(NFA *, List *, unsigned int);

void free_nfa(NFA *);
void release_nfa(NFA *);
void update_range_nfa(unsigned int, unsigned int, NFA *, int);
void inject_capturegroup_markers(NFA *, NFA *, unsigned int);
int  update_range_w_collation(char *, int, NFA *, int);

#endif
