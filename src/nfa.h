#ifndef NFA_H_
#define NFA_H_
#include <limits.h>

#define MAX_NFA_STATES 512
#define SIZE_OF_LOCALE 128
#define SIZE_OF_RANGE SIZE_OF_LOCALE/(sizeof(unsigned int) * CHAR_BIT)

#define NFA_ANY        0x001
#define NFA_LITERAL    0x002
#define NFA_RANGE      0x004
#define NFA_SPLIT      0x008
#define NFA_EPSILON    0x010
#define NFA_ACCEPTING  0x020
#define NFA_BOL_ANCHOR 0x040
#define NFA_EOL_ANCHOR 0x080
#define NFA_NGLITERAL  0x100 // negated literal as in [^abc]
#define NFA_BACKREFERENCE  0x200 // negated literal as in [^abc]



typedef unsigned int nfa_range[SIZE_OF_RANGE];

typedef struct NFACtrl {
  struct NFACtrl * ctrl_id;
  unsigned int next_seq_id;
} NFACtrl;

typedef struct NFA {
  struct NFACtrl * ctrl;
  unsigned int id;
  struct {
    unsigned int type;
    union {
      unsigned int literal;
      nfa_range * range;
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
//NFA * new_anchor_nfa(NFACtrl *, unsigned int);
NFA * new_kleene_nfa(NFA *);
NFA * new_literal_nfa(NFACtrl *, unsigned int, unsigned int);
NFA * new_posclosure_nfa(NFA *);
NFA * new_qmark_nfa(NFA *);
NFA * new_range_nfa(NFACtrl *, int);

void free_nfa(NFA *);
void update_range_nfa(unsigned int, unsigned int, NFA *, int);
int  update_range_w_collation(char *, int, NFA *, int);
//int mark_nfa(NFA *);

#endif
