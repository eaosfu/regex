#ifndef BOYER_MOORE_H_
#define BOYER_MOORE_H_

#include "slist.h"
#include "rbtree.h"
#include "match_record.h"


typedef struct BoyerMoore {
  const char * pattern;
  int * good_suffix;
  RBTreeCtrl * bad_char;
  int pattern_len;
  MatchRecordObj * matches;
  MatchRecord ** iter;
  int min;
  int max;
} BMObj;


BMObj * new_bm_obj();
void bm_obj_free(BMObj **);
void bm_init_obj(BMObj *, const char *, int);
void bm_search(BMObj *, char *, char *);
void bm_clear_matches(BMObj *);
int bm_match_count(BMObj *);
MatchRecord * bm_next_match(BMObj *);

#endif
