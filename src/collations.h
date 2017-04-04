#ifndef COLLATIONS_H_
#define COLLATIONS_H_

typedef struct {
  int low;
  int high;
} coll_ranges;

typedef struct { 
  const char * name;
  int name_len;
  int range_num;
  coll_ranges ranges[4];
} named_collations;


// If you update this, make sure to update
// collations[] in collations.c
enum {
  COLL_ALNUM=0,
  COLL_ALPHA,
  COLL_BLANK,
  COLL_CNTRL,
  COLL_DIGIT,
  COLL_GRAPH,
  COLL_LOWER,
  COLL_PRINT,
  COLL_PUNCT,
  COLL_SPACE,
  COLL_UPPER,
  COLL_XDIGIT
};

#endif
