#ifndef MPAT_H_
#define MPAT_H_

// FIXME: implementing an 'intrusive' version of this will
//        greatly improve resource utilization
//        -- see comment in 'record_match()'
#include "rbtree.h"

#include "slist.h"

#define HASH   RBTreeCtrl
#define SHIFT  RBTreeCtrl
#define PREFIX RBTreeCtrl


typedef struct PrefixPattern {
   long key;
   char pattern[];
} PrefixPattern;


typedef struct MatchRecord {
  char * beg;
  char * end;
} MatchRecord;


typedef struct MPatObj {
  HASH   * hash;
  SHIFT  * shift;
  PREFIX * prefix;
  int m;  // length of all patterns
  int B;  // pattern substring length
  int default_shift;
  long min_key;
  long max_key;
  RBTreeCtrl * match_list;
  RBTree * cur_mr;
} MPatObj;


MPatObj * new_mpat();
MatchRecord * mpat_next_match();
int  mpat_init(MPatObj *, List *);
int  mpat_match_count(MPatObj *);
void mpat_search(MPatObj *, char *, char *);
void mpat_obj_free(MPatObj **);
void mpat_clear_matches(MPatObj *);


#endif
