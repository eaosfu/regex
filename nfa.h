#ifndef NFA_H_
#define NFA_H_
#include <limits.h>

//typedef enum {LITERAL=255, SPLIT=256, EPSILON=257, ACCEPTING=258} nfa_type;

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

typedef unsigned int nfa_range[SIZE_OF_RANGE];

typedef struct NFA {
  //nfa_type type;
  struct {
    unsigned char type;
    union {
      unsigned int literal;
      nfa_range * range;
    };
  } value;
  struct NFA * parent;
  struct NFA * out1;
  struct NFA * out2;
} NFA;

NFA * new_nfa(unsigned char);
NFA * new_kleene_nfa(NFA *);
NFA * concatenate_nfa(NFA *, NFA *);
NFA * new_kleene_nfa(NFA *);
NFA * new_qmark_nfa(NFA *);
NFA * new_posclosure_nfa(NFA *);
NFA * new_alternation_nfa(NFA *, NFA *);
NFA * new_range_nfa(unsigned int, unsigned int);
void update_range_nfa(unsigned int, unsigned int, nfa_range *);

NFA * new_literal_nfa(unsigned int);
#endif
