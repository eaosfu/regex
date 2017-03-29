#include "mpat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"


#ifdef DEBUG
#define COMPUTE_SUFFIX_HASH(ptr)    (SHOW_SUFFIX_COMPUTE(ptr), ((*(ptr) << 5) + *((ptr) - 1)))
#define SHOW_SUFFIX_COMPUTE(ptr)    (printf("SUFFIX_COMPUTE: '%c", *(ptr)), \
                                     printf("%c':", *((ptr) - 1)), \
                                     printf(" %ld\n", (long)((*(ptr) << 5) + *((ptr) - 1))))
#define COMPUTE_PREFIX_HASH(ptr, m) (SHOW_PREFIX_COMPUTE(ptr, m), (*((ptr) - m + 1) << 8) + *((ptr) - m + 2))
#define SHOW_PREFIX_COMPUTE(ptr, m) (printf("PREFIX COMPUTE (%s): '%c", (ptr - m + 1), (*((ptr) - m + 1))), \
                                     printf("%c':",*((ptr) - m + 2)), \
                                     printf(" %ld\n", (long)(*((ptr) - m + 1) << 8) + *((ptr) - m + 2)))
#else
#define COMPUTE_SUFFIX_HASH(ptr)    ((((long)(*(ptr)) << (long)5) + *((ptr) - 1)))
#define COMPUTE_PREFIX_HASH(ptr, m) ((((long)(*((ptr) - m + 1) << 8)) + *((ptr) - m + 2)))
#endif



#define FREE_HASH(h, f) (rbtree_free((h), (f)))
#define FREE_SHIFT(s, f) (rbtree_free((s), (f)))
#define FREE_PREFIX(p, f) (rbtree_free((p), (f)))

#define HASH_RECORD   RBTree
#define SHIFT_RECORD  RBTree
#define PREFIX_RECORD RBTree

#define NEW_HASH_TABLE() new_rbtree()
#define NEW_SHIFT_TABLE() new_rbtree()
#define NEW_PREFIX_TABLE() new_rbtree()

#define SHIFT_INSERT(s, k, ms) rbtree_insert((s), (k), (ms), 0)
#define SHIFT_SEARCH(s, k) rbtree_search((s)->root, (k))
#define HASH_SEARCH(h, k) rbtree_search((h)->root, k)

#ifdef DEBUG_INSERT
#define HASH_APPEND_PATTERN(h, p) \
  (\
    printf("\tlist[0x%x]: pattern[0x%x]:%s\n", (int *)((h)->data), p, p),\
    list_append((List *)((h)->data), (p))\
  )
#else
  #define HASH_APPEND_PATTERN(h, p) (list_append((List *)((h)->data), (p)))
#endif
 
#define PREFIX_SEARCH(p, k) (rbtree_search((p)->root, (k)))


#ifdef debug
#define SET_MIN_SHIFT(sr, shift) ({\
  int current_shift = *((int *)((sr)->data)); \
printf("\tcurrent shift: %d -- shift: %d\n", current_shift, shift); \
  if(current_shift > (shift)) *((int *)((sr)->data)) = (shift);\
})
#else
#define SET_MIN_SHIFT(sr, shift) ({\
  int current_shift = *((int *)((sr)->data)); \
  if(current_shift > (shift)) *((int *)((sr)->data)) = (shift);\
})
#endif\


static void
free_hash(HASH * hash, VISIT_PROC_pt free_func)
{
  if(hash == NULL) {
    return;
  }

  RBTree * root = hash->root;
  RBTree * cur = rbtree_min(root);
  RBTree * next = NULL;


  if(root != NULL) {
    if(cur == root) {
      cur = rbtree_min(root->right);
    }
    while((cur != NULL)) {
      if(cur == root) {
        if(root->right != NULL) {
          cur = rbtree_min(root->right);
        }
        else {
          break;
        }
      }
      if(cur->left == NULL && cur->right == NULL) {
        if((next = cur->parent) != NULL) {
          if(cur == next->left) {
            next->left = NULL;
          }
          else {
            next->right = NULL;
          }
        }
//        rbtree_free(cur->data, free_func); // don't delete constant values
        list_free(cur->data, free_func);
        free(cur);
        cur = next;
      }
      else if(cur->right != NULL) {
        cur = rbtree_min(cur->right);
      }
    }
//    rbtree_free(root->data, free_func); // don't delete constant values
    list_free(root->data, free_func); // don't delete constant values
    free(root);
  }

  free(hash);
  return;
}


void
mpat_obj_free(MPatObj ** mpat_obj)
{
  if((*mpat_obj) == NULL) {
    return;
  }

  free_hash((*mpat_obj)->hash, (void *)free);
  rbtree_free((*mpat_obj)->shift, (void *)free);
  rbtree_free((*mpat_obj)->match_list, (void *)free);
  free(*mpat_obj);
  *mpat_obj = NULL;

  return;
}


static void *
check_prefix_match(PREFIX * p, long k, long cmp)
{
  if(p == NULL) {
    return NULL;
  }
/*
  PREFIX_RECORD * pr = PREFIX_SEARCH(p, k);
  if(pr != NULL) {
    return (*((long *)pr->data) == cmp) ? p : NULL;
  }
*/
  PrefixPattern * pfxp = (PrefixPattern *)k;
printf("pfxp->key: %ld\n", pfxp->key);
  return (pfxp->key == cmp) ? p : NULL;

  return NULL;
}


// uses hash key K as key into HASH:rbtree
// contains a list of all patterns whose suffix hashes to K
static void
insert_hash_record(HASH * h, long key, char * pattern, long prefix_hash)
{
  if(h == NULL) {
    return;
  }
  HASH_RECORD * hr = NULL;
  int pattern_len = (pattern == NULL) ? 0 : strlen(pattern);
  if((hr = HASH_SEARCH(h, key)) == NULL) {
//RBTreeCtrl * pattern_list = new_rbtree();
    List * pattern_list = new_list();
    hr = rbtree_insert(h, key, pattern_list, 0);
  }
  //HASH_APPEND_PATTERN(hr, pattern);
//  RBTreeCtrl * r = hr->data;
// TEST
  PrefixPattern * pfxp = malloc(sizeof(PrefixPattern) + pattern_len + 1);
  if(pfxp == NULL) {
    fatal("Insufficient virtual memory\n");
  }
  pfxp->key = prefix_hash;
  strncpy(pfxp->pattern, pattern, pattern_len);
  (pfxp->pattern)[pattern_len] = '\0';
  //rbtree_insert(r, pattern_len, pfxp, 0);
  HASH_APPEND_PATTERN(hr, pfxp);
// END TEST
//  rbtree_insert_reverse(r, pattern_len, pattern, 0);
}


// uses address of pattern string as key into PREFIX:rbtree
// stores hash key K of pattern's prefix
static void
insert_prefix_record(PREFIX * p, long key, char * pattern)
{
  if(p == NULL) {
    return;
  }
/*
  long * k = malloc(sizeof(long));
  if(k == NULL) {
    fatal("Insufficient virtual memory\n");
  }
  (*k) = key;
  rbtree_insert(p, (long)pattern, k, 0);
*/

  int pattern_len = strlen(pattern);
  PrefixPattern * pfxp = malloc(sizeof(PrefixPattern) + pattern_len + 1);
  if(pfxp == NULL) {
    fatal("Insufficient virtual memory\n");
  }
  pfxp->key = key;
  strncpy(pfxp->pattern, pattern, pattern_len);
  (pfxp->pattern)[pattern_len] = '\0';
  rbtree_insert(p, (long)pfxp, pfxp, 0);
}


// uses hash key K as index into SHIFT:rbtree
static void 
insert_shift_record(SHIFT * s, long key, int shift)
{
  if(s == NULL) {
    return;
  }

  SHIFT_RECORD * sr = NULL;

  if((sr = SHIFT_SEARCH(s, key)) == NULL) {
    long * ms = malloc(sizeof(long));
    if(ms == NULL) {
      fatal("Insufficient virtual memory\n");
    }
    *ms = shift;
    SHIFT_INSERT(s, key, ms);
  }
  else {
    SET_MIN_SHIFT(sr, shift);
  }
}


static int
preprocess_patterns(MPatObj * mpat_obj, List * patterns)
{
  // given a set of patterns determine what 'm' and 'B' we should use
  if(patterns == NULL) {
    return 0;
  }

  size_t m = -1;
  size_t len;

  char * pattern = NULL;
  
  list_set_iterator(patterns, 0);
  while((pattern = list_get_next(patterns)) != NULL) {
    if((len = strlen(pattern)) < m) {
      m = len;
    }
  }

  if(m < 3) { // what's the point?
    return 0;
  }
  else {
    mpat_obj->m = m;
    mpat_obj->B = 2; // the paper this is based off of suggests B = 2 or 3 for good performance
  }

  mpat_obj->default_shift = mpat_obj->m - mpat_obj->B + 1;

  return 1;
}


// create a new multi string object
MPatObj *
new_mpat()
{
  MPatObj * mpat_obj = malloc(sizeof(*mpat_obj));

  if(mpat_obj == NULL) {
    fatal("Insufficient virtual memory\n");
  }

  mpat_obj->hash = NEW_HASH_TABLE();
  mpat_obj->shift = NEW_SHIFT_TABLE();
  mpat_obj->match_list = new_rbtree();
  mpat_obj->cur_mr = NULL;

  return mpat_obj;
}


// compute hash of substrings of size B from pattern
// s is a pointer to the SHIFT table
// p is a pointer to the end of the pattern
// m is the length of the pattern
// B is the length of the pattern's substrings
static void
hash_substrings(SHIFT * s, char * p, int m, int B)
{
  if(p == NULL || s == NULL) {
    return;
  }

  int j; // shift distance to align with the end of pattern
  char * tmp = p - B + 1;
  while(tmp > (p - m + 1)) {
    j = p - tmp;
    insert_shift_record(s, COMPUTE_SUFFIX_HASH(tmp), j);
    --tmp;
  }
}


// load up all tables
int
mpat_init(MPatObj * mpat_obj, List * patterns)
{
  if(patterns == NULL || mpat_obj == NULL) {
    return 0;
  }
  
  if(preprocess_patterns(mpat_obj, patterns) == 0) {
    return 0;
  }

  int m = mpat_obj->m;
  int B = mpat_obj->B;

  long suffix_hash;
  long prefix_hash;

  char * pattern = NULL;

  list_set_iterator(patterns, 0);
  while((pattern = list_get_next(patterns))) {
    suffix_hash = COMPUTE_SUFFIX_HASH(pattern + m - 1);
    prefix_hash = COMPUTE_PREFIX_HASH(pattern + m - 1, m);
    insert_hash_record(mpat_obj->hash, suffix_hash, pattern, prefix_hash);
    insert_shift_record(mpat_obj->shift, suffix_hash, 0);
    hash_substrings(mpat_obj->shift, (pattern + m - 1), m, B);
  }

  mpat_obj->min_key = rbtree_min(mpat_obj->shift->root)->key;
  mpat_obj->max_key = rbtree_max(mpat_obj->shift->root)->key;

  return 1;
}


static inline long __attribute__((always_inline))
get_shift(MPatObj * mpat_obj, long k)
{
  if(mpat_obj->min_key > k || mpat_obj->max_key < k) {
    return mpat_obj->default_shift;
  }

  SHIFT_RECORD * sr = SHIFT_SEARCH(mpat_obj->shift, k);

  if(sr) {
    return *((long *)sr->data);
  }
  return mpat_obj->default_shift;
}


static List *
get_hash_patterns(MPatObj * mpat_obj, long k)
{
  HASH_RECORD * hr = HASH_SEARCH(mpat_obj->hash, k);

  if(hr) {
    return ((List *)hr->data);
  }

  return NULL;
}

/*
static RBTreeCtrl *
get_hash_patterns(MPatObj * mpat_obj, long k)
{
  HASH_RECORD * hr = HASH_SEARCH(mpat_obj->hash, k);
  if(hr) {
    return ((RBTreeCtrl *)hr->data);
  }
  return NULL;
}
*/


static void 
record_match(RBTreeCtrl * ml, char * b, char * e)
{
  if(ml == NULL)  {
    return;
  }

  long key = (long)b;
  RBTree * node = rbtree_insert(ml, key, NULL, 1);

  if(node->data == NULL) {
    MatchRecord * mr = malloc(sizeof(*mr));
    mr->beg = b;
    mr->end = e;
    node->data = mr;
  }
  else {
    ((MatchRecord *)node->data)->beg = b;
    ((MatchRecord *)node->data)->end = e;
  }

  return;
}



static void
print_match_record(MatchRecord * mr)
{
  if(mr == NULL) {
    return;
  }
  char * tmp = mr->beg;
  while(tmp <= mr->end) {
    printf("%c", *tmp);
    ++tmp;
  }
  printf("\n");
}


void *
get_next_match(MPatObj * mpat_obj)
{
  if(mpat_obj == NULL || mpat_obj->match_list == NULL) {
    return NULL;
  }
}

// Multi-pattern search
void
mpat_search(MPatObj * mpat_obj, char * text, char * text_end)
{
  if(mpat_obj == NULL || text == NULL) {
    return;
  }

  int m = mpat_obj->m;
  int B = mpat_obj->B;
  int shift = 0;

  char * ptr = text + m - 1;
  char * prefix = NULL;
  char * pattern = NULL;
  char * pat_ptr = NULL;
  char * prev_pat_ptr = NULL;
  char * subtxt_begin = NULL;
  long suffix_hash;
  long prefix_hash;


  PrefixPattern * hr = NULL; // hash record/entry
  List * patterns = NULL;

  while(ptr <= text_end) {
    suffix_hash = COMPUTE_SUFFIX_HASH(ptr);
    shift = get_shift(mpat_obj, suffix_hash);
    if(shift == 0) {
      shift = 1;
      prefix_hash = COMPUTE_PREFIX_HASH(ptr, m);
      patterns = get_hash_patterns(mpat_obj, suffix_hash);
      prev_pat_ptr = NULL;
      char * tmp_ptr = ptr;
      list_set_iterator(patterns, 0);
      for(hr = list_get_next(patterns); hr != NULL; hr = list_get_next(patterns)) {
        prefix = tmp_ptr - m + 1;
        subtxt_begin = prefix;
        if(hr->key == prefix_hash) {
          pattern = hr->pattern;
          pat_ptr = pattern + B - 1;
          prefix = prefix + B - 1;
          while(*(pat_ptr++) == *(prefix++));
          if(*(pat_ptr - 1) == '\0') {
            record_match(mpat_obj->match_list, subtxt_begin, prefix - 2);
          }
          else {
            if(pat_ptr < prev_pat_ptr) {
              break;
            }
            else {
              prev_pat_ptr = pat_ptr;
            }
          }
        }
        else {
          continue;
        }
      }
    }
    ptr += shift;
  }

  return;
}
/*
// Multi-pattern search
void
mpat_search(MPatObj * mpat_obj, char * text, char * text_end)
{
  if(mpat_obj == NULL || text == NULL) {
    return;
  }

  int m = mpat_obj->m;
  int B = mpat_obj->B;
  int shift = 0;

  char * ptr = text + m - 1;
  char * prefix = NULL;
  char * pattern = NULL;
  PrefixPattern * ppattern = NULL; // FIXME: choose a different name
  char * pat_ptr = NULL;
  char * prev_pat_ptr = NULL;
  char * subtxt_begin = NULL;
  long suffix_hash;
  long prefix_hash;


  RBTree * p_iterator = NULL;
  RBTreeCtrl * patterns = NULL;
  while(ptr <= text_end) {
    suffix_hash = COMPUTE_SUFFIX_HASH(ptr);
    shift = get_shift(mpat_obj, suffix_hash);
    if(shift == 0) {
      shift = 1;
      prefix_hash = COMPUTE_PREFIX_HASH(ptr, m);
      patterns = get_hash_patterns(mpat_obj, suffix_hash);
      p_iterator = rbtree_min(patterns->root);
      char * tmp_ptr = ptr;
      for(; p_iterator != NULL; p_iterator = rbtree_successor(p_iterator)) {
        prefix = tmp_ptr - m + 1;
        subtxt_begin = prefix;
        ppattern = p_iterator->data;
        if(ppattern->key != prefix_hash) continue;
        pattern = ppattern->pattern;
printf("pattern: %s vs. %s\n", pattern, text);
        pat_ptr = pattern + B - 1;
        prefix = prefix + B - 1;
        while(*(pat_ptr++) == *(prefix++));
        if(*(pat_ptr - 1) == '\0') {
          record_match(mpat_obj->match_list, subtxt_begin, prefix - 2);
        }
        else {
          if(pat_ptr < prev_pat_ptr) {
            break;
          }
          else {
            prev_pat_ptr = pat_ptr;
          }
        }
      }
    }
    ptr += shift;
  }

  return;
}
*/

int
mpat_match_count(MPatObj * mpat_obj)
{
  if(mpat_obj == NULL || mpat_obj->match_list->root == NULL) {
    return 0;
  }

  return rbtree_node_count(mpat_obj->match_list);
}


MatchRecord *
mpat_next_match(MPatObj * mpat_obj)
{
  if(mpat_obj == NULL || mpat_obj->match_list->root == NULL) {
    return NULL;
  }

  if(mpat_obj->cur_mr == NULL) {
    mpat_obj->cur_mr = rbtree_min(mpat_obj->match_list->root);
  }
  else {
    mpat_obj->cur_mr = rbtree_successor(mpat_obj->cur_mr);
  }

  return (mpat_obj->cur_mr == NULL) ? NULL : mpat_obj->cur_mr->data;
}


void
mpat_clear_matches(MPatObj * mpat_obj)
{
  if(mpat_obj == NULL || mpat_obj->match_list->root == NULL) {
    return;
  }

  mpat_obj->cur_mr = NULL;
  rbtree_clear(mpat_obj->match_list);

  return;
}
