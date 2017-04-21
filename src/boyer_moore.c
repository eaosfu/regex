#include "boyer_moore.h"
#include "misc.h"

#include <string.h>
#include <stdlib.h>


#include <stdio.h>
#ifdef DEBUG

static void
dump_tables(BMObj * obj)
{
  if(obj == NULL) {
    fprintf(stderr, "No BMObj provided\n");
    exit(EXIT_FAILURE);
  }

  if(obj->pattern == NULL) {
    fprintf(stderr, "No pattern provided\n");
    exit(EXIT_FAILURE);
  }

  if(obj->good_suffix == NULL) {
    fprintf(stderr, "Missing reference to good_suffix table\n");
    exit(EXIT_FAILURE);
  }

  if(obj->bad_char == NULL) {
    fprintf(stderr, "Missing reference to bad_char table\n");
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "Pattern: %s\n", obj->pattern);

  RBTree * r = rbtree_min(obj->bad_char->root);
  fprintf(stderr, "good char table:\n");
  while(r!= NULL) {
    fprintf(stderr, "char: %c -- shift: %d\n", r->key, *((int *)(r->data)));
    r = rbtree_successor(r);
  }
  fprintf(stderr, "\n");
  fprintf(stderr, "good suffix table:\n");
  for(int i = 0; i < obj->pattern_len; ++i) {
    fprintf(stderr, "char: %c -- shift: %d\n", obj->pattern[i], obj->good_suffix[i]);
  }
  fprintf(stderr, "\n");
}
#endif


int
bm_match_count(BMObj * obj)
{
  if(obj) {
    return MATCH_RECORD_COUNT(obj->matches);
  }
  return 0;
}


BMObj *
new_bm_obj()
{
  BMObj * obj = xmalloc(sizeof(BMObj));
  obj->bad_char = new_rbtree();
  obj->matches = new_match_record_obj();
  return obj;
}


static void
compute_suffix(BMObj * obj, char suff[], int m)
{
  int f, g, i;
  const char * pattern = obj->pattern;
  suff[m - 1] = m;
  g = m - 1;
  f = m - 2;
  for (i = m - 2; i >= 0; --i) {
    if (i > g && suff[i + m - 1 - f] < i - g) {
      suff[i] = suff[i + m - 1 - f];
    }
    else {
      if (i < g) {
        g = i;
      }
      f = i;
      while (g >= 0 && pattern[g] == pattern[g + m - 1 - f]) {
        --g;
      }
      suff[i] = f - g;
    }
  }
}


static void
compute_good_suffix(BMObj * obj)
{
  char suffix[obj->pattern_len];
  int m = obj->pattern_len;
  compute_suffix(obj, suffix, m);

  for(int i = 0; i < m; ++i) {
    obj->good_suffix[i] = m;
  }

  int j = 0;
  for(int i = m - 1;  i >= 0; --i) {
    if(suffix[i] == i + 1) {
      for(; j < m - 1 - i; ++j) {
        if(obj->good_suffix[j] == m) {
          obj->good_suffix[j] = m - 1 - i;
        }
      }
    }
  }
  for(int i = 0; i <= m -2; ++i) {
    obj->good_suffix[m -1 - suffix[i]] = m - 1 - i;
  }
}


static void
compute_bad_char(BMObj * obj)
{
  int m = obj->pattern_len;
  int s = 0;

  RBTree * record = NULL;
  for(int i = 0; i < m - 1; ++i) {
    s = m - i - 1;
    if((record = rbtree_search(obj->bad_char->root, obj->pattern[i])) == NULL) {
      int * shift = xmalloc(sizeof(int));
      *shift = s;
      rbtree_insert(obj->bad_char, obj->pattern[i], shift,  0);
    }
    else {
      *((int *)record->data) = s;
    }
  }
}


void
bm_init_obj(BMObj * obj, const char * pattern, int pattern_len)
{
  if(obj == NULL || pattern == NULL) {
    return;
  }
  obj->pattern = pattern;
  obj->pattern_len = pattern_len;
  obj->good_suffix = xmalloc(sizeof(*obj->good_suffix) * obj->pattern_len);
  for(int i = 0; i < obj->pattern_len; ++i) {
    obj->good_suffix[i] = obj->pattern_len;
  }
  compute_bad_char(obj);
  compute_good_suffix(obj);

  return;
}


static inline int
bm_get_char_shift(BMObj * obj, unsigned char c)
{
  RBTree * shift = rbtree_search(obj->bad_char->root, c);
  if(shift == NULL) {
    return obj->pattern_len;
  }
  else {
    return *((int *)(shift->data));
  }

}


MatchRecord *
bm_next_match(BMObj * obj)
{
  if(obj == NULL) {
    return NULL;
  }

  MatchRecord * ret = NULL;

  if(obj->matches->head != NULL) {
    if(obj->iter == NULL) {
      obj->iter = &(obj->matches->head->next);
      ret = obj->matches->head;
    }
    else {
      if(*(obj->iter) == obj->matches->head) {
        obj->iter = NULL;
      }
      else {
        ret = *(obj->iter);
        obj->iter = &((*(obj->iter))->next);
      }
    }
  }

  return ret;
}


void
bm_search(BMObj * obj, char * input, char * input_end)
{
  if(obj == NULL || input == NULL || input_end == NULL) {
    return;
  }

  int i;
  int j = 0;
  int bad_char_shift;
  int good_suffix_shift;
  int input_len = input_end - input + 1;
  int pattern_len = obj->pattern_len;
  int m = obj->pattern_len;
  const char * pattern = obj->pattern;

  while(j <= input_len - pattern_len) {
    for(i = m - 1; i >= 0 && pattern[i] == input[i + j]; --i);
    if(i < 0) {
      // record the match
      new_match_record(obj->matches, &input[j], &input[j + m -1]);
      j += obj->good_suffix[0];
    }
    else {
      bad_char_shift = bm_get_char_shift(obj, input[i + j]);
      bad_char_shift = bad_char_shift - m + i + 1;
      if(bad_char_shift == m) {
        j += bad_char_shift;
      }
      else {
        good_suffix_shift = obj->good_suffix[i];
        j += (good_suffix_shift >= bad_char_shift) ? good_suffix_shift : bad_char_shift;
      }
    }
  }
}


void
bm_clear_matches(BMObj * obj)
{
  if(obj == NULL) {
    return;
  }

  match_record_clear(obj->matches);
  obj->iter = NULL;
}


void
bm_obj_free(BMObj ** obj)
{
  if(obj == NULL || *obj == NULL) {
    return;
  }

  free((*obj)->good_suffix);
  rbtree_free((*obj)->bad_char, (void *)free);
  match_record_free(&((*obj)->matches));
  free(*obj);
  *obj = NULL;

  return;
}

/*
void
print_match(MatchRecord * mr)
{
  const char * tmp = mr->beg;
  while(tmp <= mr->end) {
    printf("%c", *tmp);
    ++tmp;
  }
  printf("\n");
}

int
main(void)
{
  const char * pattern ="the";
  int pattern_len = 3;
  char * input = "the quick brown fox jumps over the lazy dog the";
  int input_len = strlen(input);

  List * matches = new_list();
  BMObj * obj = new_bm_obj();
  init_bm_obj(obj, pattern, pattern_len);
  bm_search(obj, input, &(input[input_len - 1]));
printf("found %d matche(s)\n", MATCH_RECORD_COUNT(obj->matches));

  MatchRecord * mr = NULL;
  while((mr = bm_next_match(obj)) != NULL) {
    print_match(mr);
  }
  bm_clear_matches(obj);
  bm_obj_free(&obj);
  list_free(matches, (void*)free);
  return 0;
}
*/
