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



typedef unsigned int nfa_range[SIZE_OF_RANGE];

typedef struct NFA {
  //nfa_type type;
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

NFA * new_nfa(unsigned int);
NFA * new_kleene_nfa(NFA *);
NFA * concatenate_nfa(NFA *, NFA *);
NFA * new_kleene_nfa(NFA *);
NFA * new_qmark_nfa(NFA *);
NFA * new_posclosure_nfa(NFA *);
NFA * new_alternation_nfa(NFA *, NFA *);
//NFA * new_range_nfa(unsigned int, unsigned int, int);
NFA * new_range_nfa(int);
//void update_range_nfa(unsigned int, unsigned int, nfa_range *, int);
void update_range_nfa(unsigned int, unsigned int, NFA *, int);
void free_nfa(NFA *);

NFA * new_literal_nfa(unsigned int, unsigned int);
NFA * new_anchor_nfa(unsigned int);
#endif
