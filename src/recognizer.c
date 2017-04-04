// FIXME: separate nfa_sim managment from run_nfa/search related code
#include "slist.h"
#include "token.h"
#include "misc.h"
#include "nfa.h"
#include "scanner.h"
#include "recognizer.h"
#include "collations.h"

#include <stddef.h>
#include <string.h>
#include <stdio.h>

extern int ncoll;
extern named_collations collations[];

#define MATCH_GLOBALLY(ctrl)     (CTRL_FLAGS((ctrl)) & MGLOBAL_FLAG)
#define INVERT_MATCH(ctrl)       (CTRL_FLAGS((ctrl)) & INVERT_MATCH_FLAG)
#define SHOW_MATCH_LINE(ctrl)    (CTRL_FLAGS((ctrl)) & SHOW_MATCH_LINE_FLAG)
#define INDIVIDUAL_MATCHES(ctrl) ((CTRL_FLAGS((ctrl)) & SILENT_OR_LINE_FLAG) == 0)
#define BAIL_EARLY(ctrl)         (CTRL_FLAGS((ctrl)) & (INVERT_MATCH_FLAG|MGLOBAL_FLAG))
#define SILENT_OR_LINE_FLAG      (SILENT_MATCH_FLAG|INVERT_MATCH_FLAG|SHOW_MATCH_LINE_FLAG)
#define LINE_OR_INVERT(ctrl)     (CTRL_FLAGS((ctrl)) & (SHOW_MATCH_LINE_FLAG|INVERT_MATCH_FLAG))

#define INPUT_CHAR(sim)        (*((sim)->input_ptr))
#define INST_TYPE(sim)         ((sim)->ip->value.type)
#define VALUE_LITERAL(sim)     ((sim)->ip->value.literal)
#define RANGE(sim)             (*((sim)->ip->value.range))
#define EOL(sim)               ((sim)->scanner->eol_symbol)
#define CASE_FOLD(sim)         (CTRL_FLAGS((sim)->ctrl) & IGNORE_CASE_FLAG)
#define INPUT(sim)             (CASE_FOLD(sim) ? toupper(INPUT_CHAR(sim)) : INPUT_CHAR(sim))
#define INST_TOKEN(sim)        (CASE_FOLD(sim) ? toupper(VALUE_LITERAL(sim)) : VALUE_LITERAL(sim))
#define INST_LONG_TOKEN(sim)   (CASE_FOLD(sim) ? toupper(NEXT_IN_LLITERAL(sim)): NEXT_IN_LLITERAL(sim))
#define NEXT_IN_LLITERAL(sim)  ((sim)->ip->value.lliteral[(sim)->ip->value.idx])

static void load_next(NFASim *, NFA *);
static void load_start_states(NFASim ** sim, NFA * start_state);
static void process_adjacents(NFASim *sim, NFA * nfa);


static inline void
RELEASE_ALL_THREADS(NFASimCtrl * ctrl, NFASim * thread)
{
  if(list_size(ctrl->active_threads)) {
    list_transfer(ctrl->thread_pool, ctrl->active_threads);
  }
  if(thread) {
    list_append(ctrl->thread_pool, thread);
  }
}


static inline void
update_longest_match(Match * m, Match * nm)
{
  if(m->end == 0 || ((m->end - m->start) < (nm->end - nm->start))) {
    m->start = nm->start;
    m->end   = nm->end;
  }
  else if(((nm->start - m->end) > -1) && ((nm->start - m->end) <= 1)) {
    m->end = nm->end;
  }
}


static void
update_match(NFASim * sim)
{
  if(sim->match.start == 0) {
    sim->match.start = sim->input_ptr;
    sim->match.end   = sim->input_ptr;
  }
  else {
    sim->match.end = sim->input_ptr;
  }
}


static inline void __attribute__((always_inline))
fill_output_buffer(NFASimCtrl * ctrl, const char * begin, int sz, int flush_buffer)
{
#define APPEND_NEWLINE(ctrl) ({                    \
  (ctrl)->matches[(ctrl)->match_idx] = '\n',       \
  ++((ctrl)->match_idx);                           \
})

#define APPEND_NULL(ctrl) ({                       \
  (ctrl)->matches[(ctrl)->match_idx] = '\0',       \
  (ctrl)->match.last_match_end = (ctrl)->match.end; \
})

  if((ctrl->match_idx + sz + 1) >= MATCH_BUFFER_SIZE) {
    // we're in trouble here... our buffer isn't large enough to hold
    // the output even after flushing it...
    int fill_size = MATCH_BUFFER_SIZE - ctrl->match_idx - 1;
    int remain_out = sz;
    if(ctrl->match_idx != 0) {
      if(fill_size < 1) {
        flush_matches(ctrl);
        fill_size = MATCH_BUFFER_SIZE - 1;
      }
      else {
        // the match output was supposed to be precded by a filenmae, a linenumber
        // or both... fill the rest of the buffer before flushing it
        strncpy((ctrl->matches) + ctrl->match_idx, begin, fill_size);
        ctrl->match_idx = MATCH_BUFFER_SIZE - 1;
        APPEND_NULL(ctrl);
        flush_matches(ctrl); // resets ctrl->match_idx to 0
        begin += fill_size;
        remain_out -= fill_size;
        fill_size = MATCH_BUFFER_SIZE - 1;
      }
    }
    // continue filling and flushing buffer until there's nothing left to
    // print
    while(remain_out > fill_size) {
      strncpy((ctrl->matches) + ctrl->match_idx, begin, fill_size);
      ctrl->match_idx = MATCH_BUFFER_SIZE - 1;
      APPEND_NULL(ctrl);
      flush_matches(ctrl); // resets ctrl->match_idx to 0
      begin += fill_size;
      remain_out -= fill_size;
    }
    // -1 for '\n', -1 for '\0'
    fill_size = MATCH_BUFFER_SIZE - 2;

    if(remain_out != 0) {
      if(remain_out == MATCH_BUFFER_SIZE) {
        strncpy(ctrl->matches, begin, MATCH_BUFFER_SIZE - 1);
        ctrl->match_idx = MATCH_BUFFER_SIZE - 1;
        APPEND_NULL(ctrl);
        flush_matches(ctrl);
        remain_out -= MATCH_BUFFER_SIZE - 1;
      }
      strncpy(ctrl->matches, begin, remain_out);
      ctrl->match_idx = remain_out;
      APPEND_NEWLINE(ctrl);
      APPEND_NULL(ctrl);
      flush_matches(ctrl);
    }
  }
  else {
    strncpy((ctrl->matches) + ctrl->match_idx, begin, sz);
    ctrl->match_idx += sz;
    APPEND_NEWLINE(ctrl);
    APPEND_NULL(ctrl);
    if(flush_buffer) {
      flush_matches(ctrl);
    }
  }
  return;
#undef APPEND_NEWLINE
#undef APPEND_NULL
}


static void
new_match(NFASimCtrl * ctrl)
{
  if(((CTRL_FLAGS(ctrl) & SILENT_MATCH_FLAG) == 0)) {
    const char * filename = ctrl->filename;
    const char * begin    = ctrl->match.start;
    const char * end      = ctrl->match.end;
    int nm_len            = ctrl->filename_len;
    int line_no           = ctrl->scanner->line_no;
    int w                 = 1;
    int flush_buffer      = 0;
    int input_too_big     = 0;
    if(begin && (begin <= end)) {

      if((CTRL_FLAGS(ctrl) & SHOW_MATCH_LINE_FLAG)) {
        begin = ctrl->buffer_start;
        end = ctrl->buffer_end - 1;
      }

      int sz = end - begin + 1;

      if((CTRL_FLAGS(ctrl) & SHOW_FILE_NAME_FLAG) && (nm_len > 0)) {
        sz += nm_len + 1; // add ':' after <filename>
      }

      if((CTRL_FLAGS(ctrl) & SHOW_LINENO_FLAG)) {
        int tmp = line_no;
        while((tmp/10 > 0) && (tmp /= 10) && (++w));
        sz += w + 1; // add ':' after <line number>
      }

      if(ctrl->match.last_match_end) {
        // if we reach this then the match buffer hans't been flushed
        if((ctrl->match.start - ctrl->match.last_match_end) == 0) {
          ctrl->match_idx -= 2;
        }
        if((ctrl->match.start - ctrl->match.last_match_end) == 1) {
          ctrl->match_idx -= 1;
        }
      }

      input_too_big = ((ctrl->match_idx + sz + 1) >= MATCH_BUFFER_SIZE);

      // +1 for '\0'
      if((ctrl->match_idx != 0) && input_too_big) {
        flush_matches(ctrl);
        input_too_big = ((ctrl->match_idx + sz + 1) >= MATCH_BUFFER_SIZE);
      }

      if((CTRL_FLAGS(ctrl) & SHOW_FILE_NAME_FLAG) && (nm_len > 0)) {
        if(input_too_big) {
          // if we get here then we've alread flushed the output buffer
          // but still don't have enough space in the output buffer.
          // call printf and let output buffer handle printing the match
          printf("%s:", filename);
          // since the match by itself may have fit into the output buffer we need
          // to force flushing the output buffer to ensure we don't have an awkward
          // delay between printing the filename and match
          flush_buffer = 1;
        }
        else {
          snprintf(ctrl->matches + ctrl->match_idx, nm_len + 2, "%s:", filename);
          ctrl->match_idx += nm_len + 1;
        }
        sz -= nm_len + 1;
      }

      if((CTRL_FLAGS(ctrl) & SHOW_LINENO_FLAG)) {
        // see comment immediately above... where filename is printed
        if(input_too_big) {
          printf("%d:", line_no);
          flush_buffer = 1;
        }
        else {
          snprintf(ctrl->matches + ctrl->match_idx, nm_len + w + 1, "%d:", line_no);
          ctrl->match_idx += w + 1;
        }
        sz -= w + 1;
      }

      fill_output_buffer(ctrl, begin, sz, flush_buffer);
    }
  }

  return;
}


inline void
flush_matches(NFASimCtrl * ctrl)
{
//  if(((CTRL_FLAGS(ctrl) & SILENT_MATCH_FLAG) == 0)) {
    printf("%s", ctrl->matches);
    ctrl->match_idx = 0;
    ctrl->match.last_match_end = 0;
//  }
}


void *
free_match_string(void * m)
{
  if(m) {
    free(m);
  }
  return ((void *)NULL);
}


static inline int
is_literal_space(int c)
{
  int ret = 0;
  if(c > 0) {
    if(((collations[COLL_SPACE].ranges[0].low <= c) && (collations[COLL_SPACE].ranges[0].high >= c))
    || ((collations[COLL_SPACE].ranges[1].low <= c) && (collations[COLL_SPACE].ranges[1].high >= c))) {
      ret = 1;
    }
  }
  return ret;
}


static inline int
is_literal_word_constituent(int c)
{
  int ret = 0;
  if(c > 0) {
    if(((collations[COLL_ALNUM].ranges[0].low <= c) && (collations[COLL_ALNUM].ranges[0].high >= c))
    || ((collations[COLL_ALNUM].ranges[1].low <= c) && (collations[COLL_ALNUM].ranges[1].high >= c))
    || ((collations[COLL_ALNUM].ranges[2].low <= c) && (collations[COLL_ALNUM].ranges[2].high >= c))) {
      ret = 1;
    }
  }

  return ret;
}


static inline int
is_literal_in_range(nfa_range range, int c)
{
  int ret = 0;
  if(c > 0) {
    BIT_MAP_TYPE mask = set_bit(BIT_MAP_TYPE, BITS_PER_BLOCK, c)|0;
    if(range[get_bit_array_idx(c, BITS_PER_BLOCK)] & mask) {
      ret = 1;
    }
  }
  return ret;
}


static inline NFASim * __attribute__((always_inline))
reset_thread(NFASimCtrl * ctrl, NFA * start_state, char * start_pos)
{
  NFASim * sim = NULL;
  if(list_size(ctrl->thread_pool)) {
    sim                               = list_shift(ctrl->thread_pool);
    sim->ip                           = start_state;
    sim->input_ptr                    = start_pos;
    sim->match.end                    = NULL;
    sim->match.start                  = NULL;
    sim->tracking_backrefs            = 0;
    sim->ctrl->match.last_match_end   = NULL;
    restart_from(sim->scanner, start_pos);
    memset(sim->loop_record, 0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
    load_start_states(&sim, start_state);
  }
  else {
    fatal("Unable to obtain an execution threads\n");
  }
  return sim;
}

/*
static int
check_match(NFASim * sim, int nfa_type)
{
  if(CTRL_FLAGS(sim->ctrl) & IGNORE_CASE_FLAG) {
  }

  switch(nfa_type) {
    case NFA_LITERAL: {
    } break;
    case NFA_LONG_LITERAL: {
    } break;
    case NFA_RANGE: {
    } break;
    case NFA_ANY: {
    }
  }
}
*/

static inline void *
thread_clone(NFA * nfa, NFASim * sim)
{
  NFASim * clone;
  NFASimCtrl * ctrl = sim->ctrl;

  int new_thread = 0;

  if(nfa->value.type == NFA_LITERAL) {
    if(nfa->value.literal != INPUT(sim)) {
      return NULL;
    }
  }
  if(nfa->value.type == NFA_LONG_LITERAL) {
    if(*(nfa->value.lliteral) != INPUT(sim)) {
      return NULL;
    }
  }
  if(nfa->value.type == NFA_RANGE) {
    if(is_literal_in_range(*(nfa->value.range), INPUT(sim)) == 0) {
      return NULL;
    }
  }

  if(list_size(ctrl->thread_pool)) {
    clone = list_shift(ctrl->thread_pool);
  }
  else {
    new_thread = 1;
    clone = xmalloc(sim->size);
  }

  clone->status             = 0;
  clone->ip                 = nfa;
  clone->size               = sim->size;
  clone->input_ptr          = sim->input_ptr;
  clone->ctrl               = sim->ctrl;
  clone->scanner            = sim->scanner;
  clone->match              = sim->match;
  clone->tracking_backrefs  = sim->tracking_backrefs;

  clone->interval_count = sim->interval_count;

  if(sim->tracking_intervals) {
    memcpy(clone->loop_record, sim->loop_record, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
    clone->tracking_intervals = sim->tracking_intervals;
  }
  else if((new_thread == 0) && (clone->tracking_intervals != 0)) {
    memset(clone->loop_record, 0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
    clone->tracking_intervals = 0;
  }

  if((clone->tracking_backrefs > 0) && (new_thread == 0)) {
    memcpy(clone->backref_match, sim->backref_match, sizeof(Match) * CGRP_MAX);
  }

  load_next(clone, nfa);

  if(clone->status == -1) {
    list_append(ctrl->thread_pool, clone);
    clone = NULL;
  }
  else {
    list_append(ctrl->active_threads, clone);
  }

  return clone;
}


static inline NFASim *
release_thread(NFASim * sim, NFA * start_state)
{
  list_push(sim->ctrl->thread_pool, sim);
  return list_shift(sim->ctrl->active_threads);
}


static void
load_next(NFASim * sim, NFA * nfa)
{
  NFASimCtrl * ctrl = sim->ctrl;
  switch(nfa->value.type) {
    case NFA_CAPTUREGRP_BEGIN: {
      sim->tracking_backrefs = 1;
      sim->backref_match[nfa->id].start = sim->input_ptr;
      sim->backref_match[nfa->id].end = NULL;
      process_adjacents(sim, nfa);
    } break;
    case NFA_CAPTUREGRP_END: {
      sim->backref_match[nfa->id].end = sim->input_ptr - 1;
      process_adjacents(sim, nfa);
    } break;
    case NFA_INTERVAL: {
      ++(sim->interval_count);
      int start, end, far_end = list_size(&(nfa->reachable)) - 1;
      int count = ++((sim->loop_record[nfa->id]).count);
      // check the status bit for loop count if dirty then memset it 0
      NFA * tmp = NULL;
      if(count < nfa->value.min_rep) {
        start = 1;
        end = (nfa->value.split_idx - 1);
        ++(sim->tracking_intervals);
        tmp = list_get_at(&(nfa->reachable), 0);
        list_iterate_from_to(&(nfa->reachable), start, end, (void *)thread_clone, (void *)sim);
        load_next(sim, tmp);
      }
      else {
        if(nfa->value.max_rep > 0) {
          if(count < nfa->value.max_rep) {
            ++(sim->tracking_intervals);
            end = nfa->value.split_idx - 1;
            if(CHECK_NFA_CYCLE_FLAG(nfa)) {
              if(ctrl->last_interval_pos == &INPUT_CHAR(sim)) {
                sim->loop_record[nfa->id].count = 0;
                --(sim->tracking_intervals);
                start = nfa->value.split_idx + 1; // list_get_at is zero based!
                tmp = list_get_at(&(nfa->reachable), nfa->value.split_idx);
                list_iterate_from_to(&(nfa->reachable), start, far_end, (void *)thread_clone, (void *)sim);
                load_next(sim, tmp);
                break;
              }
              ctrl->last_interval_pos = &INPUT_CHAR(sim);
            }
            list_iterate_from_to(&(nfa->reachable), 0, end, (void *)thread_clone, (void *)sim);
            sim->loop_record[nfa->id].count = 0;
            --(sim->tracking_intervals);
            start = nfa->value.split_idx + 1; // list_get_at is zero based!
            tmp = list_get_at(&(nfa->reachable), nfa->value.split_idx);
            list_iterate_from_to(&(nfa->reachable), start, far_end, (void *)thread_clone, (void *)sim);
            load_next(sim, tmp);
          }
          else {
            --(sim->tracking_intervals);
            sim->loop_record[nfa->id].count = 0;
            if(CHECK_NFA_CYCLE_FLAG(nfa)) {
              tmp = list_get_at(&(nfa->reachable), 0);
              list_iterate_from_to(&(nfa->reachable), 1, far_end, (void *)thread_clone, (void *)sim);
              load_next(sim, tmp);
            }
            else {
              start = nfa->value.split_idx + 1; // list_get_at is zero based!
              tmp = list_get_at(&(nfa->reachable), nfa->value.split_idx);
              list_iterate_from_to(&(nfa->reachable), start, far_end, (void *)thread_clone, (void *)sim);
              load_next(sim, tmp);
            }
          }
        }
        else {
          // unbounded upper limit
          ++(sim->tracking_intervals);
          end = nfa->value.split_idx - 1;
          list_iterate_from_to(&(nfa->reachable), 0, end, (void *)thread_clone, (void *)sim);

          if(CHECK_NFA_ACCEPTS_FLAG(nfa) == 0) {
            sim->loop_record[nfa->id].count = 0;
          }

          start = nfa->value.split_idx + 1; // list_get_at is zero based!
          tmp = list_get_at(&(nfa->reachable), nfa->value.split_idx);
          list_iterate_from_to(&(nfa->reachable), start, far_end, (void *)thread_clone, sim);
          load_next(sim, tmp);
        }
      }
    } break;
    case NFA_ACCEPTING: {
      sim->ip = nfa;
      sim->status = 1;
    } break;
    default: {
      int match   =  1;
      sim->status = -1;
      if(sim->tracking_intervals || ((ctrl->active_threads_sp)[nfa->id] != sim->input_ptr)) {
        if(nfa->value.type == NFA_LITERAL) {
          match = (nfa->value.literal == INPUT(sim));
        }
        else if(nfa->value.type == NFA_LONG_LITERAL) {
          match = ((nfa->value.lliteral)[0] == INPUT(sim));
        }
        else if(nfa->value.type == NFA_RANGE) {
          match = is_literal_in_range(*(nfa->value.range), INPUT(sim));
        }
        else {
          sim->ip = nfa;
          sim->status =  0;
        }

        if(match) {
          (ctrl->active_threads_sp)[nfa->id] = sim->input_ptr;
          sim->ip = nfa;
          sim->status =  0;
        }
      }
    }
  }
}


static void inline
process_adjacents(NFASim *sim, NFA * nfa)
{
  for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
    thread_clone(list_get_at(&(nfa->reachable), i), sim);
  }
  load_next(sim, list_get_at(&(nfa->reachable), 0));
}


static int
thread_step(NFASim * sim)
{
  NFASimCtrl * ctrl = sim->ctrl;
  (ctrl->active_threads_sp)[sim->ip->id] = NULL;
  if(INPUT(sim) != '\0') {
    switch(INST_TYPE(sim)) {
      case NFA_ANY: {
        if(INPUT(sim) != EOL(sim)) {
          update_match(sim);
          ++(sim->input_ptr);
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_LITERAL: {
        if(INPUT(sim) == INST_TOKEN(sim)) {
          update_match(sim);
          ++(sim->input_ptr);
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_LONG_LITERAL: {
        int match = 0;
        sim->ip->value.idx = 0;
        while(INPUT(sim) == INST_LONG_TOKEN(sim)) {
          match = 1;
          update_match(sim);
          ++(sim->input_ptr);
          sim->ip->value.idx = ((sim->ip->value.idx) + 1) % sim->ip->value.len;
          if(sim->ip->value.idx == 0) {
            break;
          }
        }
        if(match && (sim->ip->value.idx == 0)) {
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->ip->value.idx = 0;
          sim->status = -1;
        }
      } break;
      case NFA_RANGE: {
        if(INPUT(sim) != EOL(sim)) {
          if(is_literal_in_range(RANGE(sim), INPUT(sim))) {
            update_match(sim);
            ++(sim->input_ptr);
            process_adjacents(sim, sim->ip);
          }
          else {
            sim->status = -1;
          }
          break;
        }
        sim->status = -1;
      } break;
      case NFA_BACKREFERENCE: {
        #define BACKREF_START(sim) ((sim)->backref_match[(sim)->ip->id].start)
        #define BACKREF_END(sim) ((sim)->backref_match[(sim)->ip->id].end)
        const char * bref_end = BACKREF_END(sim);
        const char * tmp_input = sim->input_ptr;
        const char * bref_ptr = BACKREF_START(sim);
        if(bref_ptr && bref_end) {
          int fail = 0;
          while(bref_ptr <= bref_end) {
            if(*bref_ptr == *tmp_input) {
              ++bref_ptr; ++tmp_input;
            }
            else {
              fail = 1;
              break;
            }
          }
          if(fail) {
            sim->status = -1;
          }
          else {
            sim->input_ptr = tmp_input - 1;
            update_match(sim);
            ++(sim->input_ptr);
            process_adjacents(sim, sim->ip);
          }
        }
        else {
          sim->status = -1;
        }
        #undef BACKREF_START
        #undef BACKREF_END
      } break;
      case NFA_BOL_ANCHOR: {
        if(sim->input_ptr == sim->scanner->buffer) {
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_EOL_ANCHOR: {
        if(INPUT(sim) == EOL(sim)) {
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_WORD_BEGIN_ANCHOR: {
        sim->status = -1;
        if(sim->input_ptr == sim->scanner->buffer) {
          // we're at BOL
          if(is_literal_word_constituent(INPUT(sim))) {
            // the current character is a [_[:alnum:]]
            process_adjacents(sim, sim->ip);
          }
        }
        else {
          if(is_literal_word_constituent(INPUT(sim)) == 0) {
            // the current character is a [[:space:]]
            ++(sim->input_ptr);
            if(is_literal_word_constituent(INPUT(sim))) {
              // the current character is a [_[:alnum:]]
              process_adjacents(sim, sim->ip);
            }
          }
        }
      } break;
      case NFA_WORD_END_ANCHOR: {
        sim->status = -1;
        if(is_literal_word_constituent(INPUT(sim)) == 0) {
          if(sim->input_ptr != sim->scanner->buffer) {
            int c = *(sim->input_ptr - 1);
            if(is_literal_word_constituent(c)) {
              process_adjacents(sim, sim->ip);
            }
          }
        }
      } break;
      case NFA_WORD_BOUNDARY: {
        sim->status = -1;
        int c;
        if(sim->input_ptr == sim->scanner->buffer) {
          c = *(sim->input_ptr + 1);
          if(is_literal_word_constituent(c)) {
            process_adjacents(sim, sim->ip);
          }
        }
        else if(INPUT(sim) == EOL(sim)) {
          c = *(sim->input_ptr - 1);
          if(is_literal_word_constituent(c)) {
            process_adjacents(sim, sim->ip);
          }
        }
        else {
          if(is_literal_word_constituent(INPUT(sim)) == 0) {
            if(is_literal_word_constituent(*(sim->input_ptr - 1))
            || is_literal_word_constituent(*(sim->input_ptr + 1))) {
              ++(sim->input_ptr);
              process_adjacents(sim, sim->ip);
            }
          }
        }
      } break;
      case NFA_NOT_WORD_BOUNDARY: {
        // FIXME:
        sim->status = -1;
      } break;
      case NFA_ACCEPTING: {
        sim->status = 1;
      } break;
      default: {
        sim->status = -1;
      }
    }
  }
  else {
    sim->status = 1;
  }

  return sim->status;
}


static void
load_start_states(NFASim ** sim, NFA * start_state) {
  process_adjacents((*sim), start_state);
  if((*sim)->status == -1) {
    *sim = release_thread(*sim, start_state);
  }
}


// run an initial scan on the input to see if we have a chance
// at matching
static void
find_keyword_start_pos(NFASimCtrl * ctrl, NFASim ** sim)
{
  MPatObj * mpat_obj = ctrl->mpat_obj;
  char * text_beg = (char *)ctrl->buffer_start;
  char * text_end = (char *)ctrl->buffer_end;
  mpat_search(mpat_obj, text_beg, text_end);
  (*sim)->input_ptr = ctrl->buffer_end + 1;
  if(mpat_match_count(mpat_obj) > 0) {
    MatchRecord * mr = mpat_next_match(mpat_obj);
    (*sim)->input_ptr = mr->beg;
    load_start_states(sim, ctrl->start_state);
  }
}


static void
update_input_pointer(NFASimCtrl * ctrl, int current_run, const char ** input_pointer)
{
  if(ctrl->mpat_obj == NULL) {
    if((current_run == 0) || (ctrl->match.end == 0)) {
      // we either didn't match anything: current_match <-- 0 && ctrl->match.end <-- 0
      // or we matched the empty string:  current_match <-- 1 && ctrl->match.end <-- 0
      ++(*input_pointer);
    }
    else {
      // we matched a nonempty string: current_match <-- 1 && ctrl->match.end <-- 1
      // in thise case ctrl->match.end is the 'end position' of the latest match
      *input_pointer = ctrl->match.end + 1;
    }
  }
  else {
    MatchRecord * mr = NULL;
    if((current_run == 0) || (ctrl->match.end == 0)) {
      // we either didn't match anything: current_match <-- 0 && ctrl->match.end <-- 0
      // or we matched the empty string:  current_match <-- 1 && ctrl->match.end <-- 0
      if((mr = mpat_next_match(ctrl->mpat_obj)) != NULL) {
        (*input_pointer) = mr->beg;;
      }
      else {
        (*input_pointer) = "\0"; // force the recognizer to quit
      }
    }
    else {
      // we matched a nonempty string: current_match <-- 1 && ctrl->match.end <-- 1
      // in thise case ctrl->match.end is the 'end position' of the latest match
      while((mr = mpat_next_match(ctrl->mpat_obj)) != NULL && (mr->beg <= (ctrl->match.end + 1)));
      if(mr != NULL) {
        *input_pointer = mr->beg;
      }
      else {
        (*input_pointer) = "\0"; // force the recognizer to quit
      }
    }
  }
}


int
run_nfa(NFASim * thread)
{
  int match_found            = 0; // a match was found at some point
  int current_run            = 0; // did we match in the current run?
  NFASimCtrl * ctrl          = thread->ctrl;
  ctrl->last_interval_pos    = 0;
  thread->input_ptr          = ctrl->buffer_start;
  const char * input_pointer = ctrl->buffer_start;
  enum {NO_MATCH= -1, CONTINUE, MATCH};

  
  if(ctrl->mpat_obj != NULL) {
    find_keyword_start_pos(ctrl, &thread);
    if(thread == NULL) {
      // there is no way we will ever match this line
      goto RELEASE_ALL_THREADS;
    }
    input_pointer = thread->input_ptr;
  }
  else {
    // don't prescan the input
    load_start_states(&thread, ctrl->start_state);
  }
  while((*input_pointer) != '\0') {
    while(thread) {
      thread_step(thread);
      switch(thread->status) {
        case MATCH: {
          current_run = match_found = 1;
          if(INDIVIDUAL_MATCHES(ctrl)) {
            // the -o command-line option was specified
            // the -q commane-line was NOT specified... if it had we'd
            // BAIL_EARLY
            update_longest_match(&(ctrl->match), &(thread->match));
            if(MATCH_GLOBALLY(ctrl) == 0) {
              // the -g command-line option was specified
              new_match(ctrl);
              goto RELEASE_ALL_THREADS;
            }
          }
          else if(BAIL_EARLY(ctrl) == 0) {
            goto RELEASE_ALL_THREADS;
          }
        } // fall through
        case NO_MATCH: {
          thread = release_thread(thread, ctrl->start_state);
        } break;
        case CONTINUE: {
          if(list_size(ctrl->active_threads) > 0) {
            list_append(ctrl->active_threads, thread);
            thread = list_shift(ctrl->active_threads);
          }
        } break;
      }
    }

    if(LINE_OR_INVERT(ctrl) == 0) {
      // the -o command-line option was specified
      new_match(ctrl);
    }

    update_input_pointer(ctrl, current_run, &input_pointer);
    ctrl->last_interval_pos = 0;
    current_run = 0;
    ctrl->match.start = NULL;
    ctrl->match.end = NULL;
    thread = reset_thread(ctrl, ctrl->start_state, (char *)input_pointer);
  }
RELEASE_ALL_THREADS:
  RELEASE_ALL_THREADS(ctrl,thread);

  if(((match_found == 0) && INVERT_MATCH(ctrl))
  || ((match_found == 1) && SHOW_MATCH_LINE(ctrl))) {
    ctrl->match.start = ctrl->buffer_start;
    ctrl->match.end   = ctrl->buffer_end - 1;
    new_match(ctrl);
  }

  if(ctrl->mpat_obj != NULL) {
    mpat_clear_matches(ctrl->mpat_obj);
  }

  return match_found;
}


NFASimCtrl *
new_nfa_sim(Parser * parser, Scanner * scanner, ctrl_flags * cfl)
{
  NFASim * sim;
  int sz    = sizeof(*sim) + (sizeof(*(sim->loop_record)) * (parser->interval_count));
  sim       = xmalloc(sz);
  sim->size = sz;
  sim->ctrl = xmalloc(sizeof(*sim->ctrl) + (sizeof(char *) * parser->total_nfa_ids + 1));
  sim->ip                    = ((NFA *)peek(parser->symbol_stack))->parent;
  sim->scanner               = scanner;
  sim->ctrl->scanner         = scanner;
  sim->ctrl->loop_record_cap = parser->interval_count;
  sim->ctrl->active_threads  = new_list();
  sim->ctrl->thread_pool     = new_list();
  sim->ctrl->ctrl_flags      = cfl;
  sim->ctrl->start_state     = sim->ip;
  sim->ctrl->mpat_obj        = parser->mpat_obj;
  list_push(sim->ctrl->thread_pool, sim);

  return sim->ctrl;
}


NFASim *
reset_nfa_sim(NFASimCtrl * ctrl)
{
  ctrl->match.last_match_end = NULL;
  ctrl->buffer_start         = get_cur_pos(ctrl->scanner);
  ctrl->buffer_end           = get_buffer_end(ctrl->scanner) - 1;
  ctrl->filename             = get_filename(ctrl->scanner);
  ctrl->filename_len         = strlen(ctrl->filename);
  ctrl->match.start          = NULL;
  ctrl->match.end            = NULL;
  NFASim * sim               = NULL;
  if(list_size(ctrl->thread_pool)) {
    sim = list_shift(ctrl->thread_pool);
    sim->match.end                  = NULL;
    sim->match.start                = NULL;
  }
  else {
    fatal("Unable to obtain thread for execution\n");
  }
  memset(sim->loop_record, 0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
  return sim;
}


void
free_nfa_sim(NFASimCtrl * ctrl)
{
  if(ctrl == NULL) {
    return;
  }

  if(list_size((ctrl->active_threads))) {
    while(list_size((ctrl->active_threads))) {
      list_push(ctrl->thread_pool, list_shift(ctrl->active_threads));
    }
  }
  list_free(ctrl->active_threads, NULL);
  list_free(ctrl->thread_pool, (void *)&free);
  free(ctrl);
}
