#ifndef NFA_H_
#define NFA_H_
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
#define NFA_INTERVAL         0x00200
#define NFA_BACKREFERENCE    0x00400 
#define NFA_CAPTUREGRP_END   0x00800 
#define NFA_CAPTUREGRP_BEGIN 0x01000 
#define NFA_TREE             0x02000 
#define NFA_LONG_LITERAL     0x04000
#define NFA_PROGRESS         0x08000

#define NFA_WORD_BEGIN_ANCHOR 0x00100
#define NFA_WORD_END_ANCHOR   ~(NFA_WORD_BEGIN_ANCHOR)
#define NFA_WORD_BOUNDARY     0x10000
#define NFA_NOT_WORD_BOUNDARY ~(NFA_WORD_BOUNDARY)

#define DONE    0x01
#define CYCLE   0x02
//#define GREEDY  0x04 // FIXME: we're not currently using this anywhere..
#define VISITED 0x08
#define ACCEPTS 0x10

#define SET_NFA_DONE_FLAG(n)      ((n)->flags |= DONE)
#define CLEAR_NFA_DONE_FLAG(n)    ((n)->flags &= ~DONE)

#define SET_NFA_CYCLE_FLAG(n)     ((n)->flags |= CYCLE)
#define CLEAR_NFA_CYCLE_FLAG(n)   ((n)->flags &= ~CYCLE)

#define SET_NFA_GREEDY_FLAG(n)    ((n)->flags |= GREEDY)
#define CLEAR_NFA_GREEDY_FLAG(n)  ((n)->flags &= ~GREEDY)

#define SET_NFA_VISITED_FLAG(n)   ((n)->flags |= VISITED)
#define CLEAR_NFA_VISITED_FLAG(n) ((n)->flags &= ~VISITED)

#define SET_NFA_ACCEPTS_FLAG(n)   ((n)->flags |= ACCEPTS)
#define CLEAR_NFA_ACCEPTS_FLAG(n) ((n)->flags &= ~ACCEPTS)

#define CHECK_NFA_DONE_FLAG(n)    ((n)->flags & DONE)
#define CHECK_NFA_CYCLE_FLAG(n)   ((n)->flags & CYCLE)
#define CHECK_NFA_ACCEPTS_FLAG(n) ((n)->flags & ACCEPTS)
#define CHECK_NFA_VISITED_FLAG(n) ((n)->flags & VISITED)

#define SIZE_OF_RANGE (SIZE_OF_LOCALE/BITS_PER_BLOCK)
#define NEXT_NFA_ID(ctrl)  (((ctrl) == NULL) ? -1 : (ctrl)->next_seq_id)

// In future, this will need to be compplemented by some other data
// structure in order to support more than just ASCII
typedef BIT_MAP_TYPE nfa_range[SIZE_OF_RANGE];

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
  int flags;
  List reachable;
  unsigned int id;
  struct {
    unsigned int type;
    union {
      struct {
        int min_rep;
        int max_rep;
        int split_idx;
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

NFA * new_qmark_nfa(NFA *);
NFA * new_kleene_nfa(NFA *);
NFA * new_posclosure_nfa(NFA *);
NFA * new_range_nfa(NFACtrl *, int);
NFA * concatenate_nfa(NFA *, NFA *);
NFA * new_nfa(NFACtrl *, unsigned int);
NFA * new_backreference_nfa(NFACtrl *, unsigned int);
NFA * new_lliteral_nfa(NFACtrl *, char *, unsigned int);
NFA * new_interval_nfa(NFA *, unsigned int, unsigned int, NFA **, NFA **);
NFA * new_literal_nfa(NFACtrl *, unsigned int, unsigned int);
NFA * new_alternation_nfa(NFACtrl *, List *, unsigned int, NFA *);

void mark_nfa(NFA *);
void free_nfa(NFA *);
void release_nfa(NFA *);
int  update_range_w_collation(char *, int, NFA *, int);
void update_range_nfa(unsigned int, unsigned int, NFA *, int);

#endif
