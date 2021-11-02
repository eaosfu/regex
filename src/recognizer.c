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

#define MATCH_GLOBALLY(ctrl)     (CTRL_FLAGS((ctrl)) & MGLOBAL_FLAG)
#define INVERT_MATCH(ctrl)       (CTRL_FLAGS((ctrl)) & INVERT_MATCH_FLAG)
#define SHOW_MATCH_LINE(ctrl)    (CTRL_FLAGS((ctrl)) & SHOW_MATCH_LINE_FLAG)
#define INDIVIDUAL_MATCHES(ctrl) ((CTRL_FLAGS((ctrl)) & SILENT_OR_LINE_FLAG) == 0)
#define BAIL_EARLY(ctrl)         (CTRL_FLAGS((ctrl)) & (INVERT_MATCH_FLAG|MGLOBAL_FLAG))
#define SILENT_OR_LINE_FLAG      (SILENT_MATCH_FLAG|INVERT_MATCH_FLAG|SHOW_MATCH_LINE_FLAG)
#define LINE_OR_INVERT(ctrl)     (CTRL_FLAGS((ctrl)) & (SHOW_MATCH_LINE_FLAG|INVERT_MATCH_FLAG))

#define INPUT_CHAR(sim)          (*((sim)->ctrl->cur_pos))
#define INST_TYPE(sim)           ((sim)->ip->value.type)
#define VALUE_LITERAL(sim)       ((sim)->ip->value.literal)
#define RANGE(sim)               (*((sim)->ip->value.range))
#define EOL(sim)                 ((sim)->scanner->eol_symbol)
#define CASE_FOLD(sim)           (CTRL_FLAGS((sim)->ctrl) & IGNORE_CASE_FLAG)
#define INPUT(sim)               (CASE_FOLD(sim) ? toupper(INPUT_CHAR(sim)) : INPUT_CHAR(sim))
#define INST_TOKEN(sim)          (CASE_FOLD(sim) ? toupper(VALUE_LITERAL(sim)) : VALUE_LITERAL(sim))

extern int ncoll;
extern named_collations collations[];

static void load_next(NFASim *, NFA *, SimPoolList *, const char *);
static void load_start_states(NFASimCtrl * sim, NFA * start_state, const char *);
static void process_adjacents(NFASim *sim, NFA * nfa, SimPoolList *, const char *);


static void
swap_next_w_active(NFASimCtrl * ctrl)
{
  ctrl->active_threads.head = ctrl->next_threads.head;
  ctrl->active_threads.tail = ctrl->next_threads.tail;
  ctrl->next_threads.head = NULL;
  ctrl->next_threads.tail = NULL;
}


static NFASim *
sim_pool_shift(SimPoolList * pool)
{
  NFASim * ret = NULL;
  if(pool->head != NULL) {
    ret = pool->head;
    if(pool->head == pool->tail) {
      pool->tail = NULL;
      pool->head = NULL;
    }
    else {
      pool->head = ret->next;
    }
    ret->next = NULL;
  }
  return ret;
}


static void
sim_pool_append(SimPoolList * pool, NFASim * thread)
{
  if(pool->head == NULL) {
    pool->head = thread;
    pool->tail = thread;
  }
  else {
    pool->tail->next = thread;
    pool->tail = thread;
  }

  thread->next = NULL;
}


static void
release_all_threads(NFASimCtrl * ctrl, NFASim * thread)
{
  if(ctrl->thread_pool.head == NULL) {
    if(ctrl->active_threads.head != NULL) {
      ctrl->thread_pool.head = ctrl->active_threads.head;
      ctrl->thread_pool.tail = ctrl->active_threads.tail;
      if(ctrl->next_threads.head != NULL) {
        ctrl->thread_pool.tail->next = ctrl->next_threads.head;
        ctrl->thread_pool.tail = ctrl->next_threads.tail;
      }
      if(thread) {
        ctrl->thread_pool.tail->next = thread;
        ctrl->thread_pool.tail = thread;
      }
    }
    else { //  'thread_pool' and 'active_threads' are empty
      if(ctrl->next_threads.head != NULL) {
        ctrl->thread_pool.head = ctrl->next_threads.head;
        ctrl->thread_pool.tail = ctrl->next_threads.tail;
        if(thread) {
          ctrl->thread_pool.tail->next = thread;
          ctrl->thread_pool.tail = thread;
        }
      }
      else { // all pools are empty
        if(thread) {
          ctrl->thread_pool.head = thread;
          ctrl->thread_pool.tail = thread;
        }
      }
    }
  }
  else { // 'thread_pool' is NOT empty
    if(ctrl->active_threads.head != NULL) {
      ctrl->thread_pool.tail->next = ctrl->active_threads.head;
      ctrl->thread_pool.tail = ctrl->active_threads.tail;
      if(ctrl->next_threads.head != NULL) {
        ctrl->thread_pool.tail->next = ctrl->next_threads.head;
        ctrl->thread_pool.tail = ctrl->next_threads.tail;
      }
    }
    else { // 'thread_pool' is NOT empty but 'active_threads' is empty
      if(ctrl->next_threads.head != NULL) {
        ctrl->thread_pool.tail->next = ctrl->next_threads.head;
        ctrl->thread_pool.tail = ctrl->next_threads.tail;
      }
    }
    if(thread) {
      ctrl->thread_pool.tail->next = thread;
      ctrl->thread_pool.tail = thread;
    }
  }

  if(thread) {
    thread->next = NULL;
  }

  ctrl->next_threads.head   = NULL;
  ctrl->next_threads.tail   = NULL;
  ctrl->active_threads.head = NULL;
  ctrl->active_threads.tail = NULL;
  return;
}


static void
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
    sim->match.start = sim->ctrl->cur_pos;
    sim->match.end   = sim->ctrl->cur_pos;
  }
  else {
    sim->match.end = sim->ctrl->cur_pos;
  }
}


static void
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


void
flush_matches(NFASimCtrl * ctrl)
{
  printf("%s", ctrl->matches);
  ctrl->match_idx = 0;
  ctrl->match.last_match_end = 0;
}


void *
free_match_string(void * m)
{
  if(m) {
    free(m);
  }
  return ((void *)NULL);
}


static int
is_literal_space(unsigned int c)
{
  int ret = 0;
  if(c < SIZE_OF_LOCALE) {
    if(((collations[COLL_SPACE].ranges[0].low <= c) && (collations[COLL_SPACE].ranges[0].high >= c))
    || ((collations[COLL_SPACE].ranges[1].low <= c) && (collations[COLL_SPACE].ranges[1].high >= c))) {
      ret = 1;
    }
  }
  return ret;
}


static int
is_literal_word_constituent(unsigned int c)
{
  int ret = 0;
  if(c < SIZE_OF_LOCALE) {
    if(((collations[COLL_ALNUM].ranges[0].low <= c) && (collations[COLL_ALNUM].ranges[0].high >= c))
    || ((collations[COLL_ALNUM].ranges[1].low <= c) && (collations[COLL_ALNUM].ranges[1].high >= c))
    || ((collations[COLL_ALNUM].ranges[2].low <= c) && (collations[COLL_ALNUM].ranges[2].high >= c))) {
      ret = 1;
    }
  }

  return ret;
}


static int
is_literal_in_range(nfa_range range, unsigned int c)
{
  int ret = 0;
  if(c < SIZE_OF_LOCALE) {
    BIT_MAP_TYPE mask = z_set_bit(BIT_MAP_TYPE, BITS_PER_BLOCK, c);
    if(range[z_get_bit_array_idx(c, BITS_PER_BLOCK)] & mask) {
      ret = 1;
    }
  }
  return ret;
}


static NFASim *
reset_thread(NFASimCtrl * ctrl, NFA * start_state, char * start_pos)
{
  if(ctrl->thread_pool.head != NULL) {
    restart_from(ctrl->scanner, start_pos);
    load_start_states(ctrl, start_state, start_pos);
  }
  else {
    fatal("Unable to obtain an execution threads\n");
  }
  return sim_pool_shift(&(ctrl->active_threads));
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
    case NFA_RANGE: {
    } break;
    case NFA_ANY: {
    }
  }
}
*/

static void *
thread_clone(NFA * nfa, NFASim * sim, SimPoolList * dst, const char * lookahead)
{
  NFASim * clone;
  NFASimCtrl * ctrl = sim->ctrl;

  int new_thread = 0;

  if(nfa->value.type == NFA_LITERAL) {
    if(nfa->value.literal != *lookahead) {
      return NULL;
    }
  }
  if(nfa->value.type == NFA_RANGE) {
    if(is_literal_in_range(*(nfa->value.range), *lookahead) == 0) {
      return NULL;
    }
  }

  if(ctrl->thread_pool.head != NULL) {
    clone = sim_pool_shift(&(ctrl->thread_pool));
  }
  else {
    new_thread = 1;
    clone = xmalloc(ctrl->size);
  }

  clone->status             = 0;
  clone->ip                 = nfa;
  clone->bref_ptr           = NULL;
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

  load_next(clone, nfa, dst, lookahead);

  if(clone->status == -1) {
    sim_pool_append(&(ctrl->thread_pool), clone);
    clone = NULL;
  }
  return clone;
}


static NFASim *
release_thread(NFASimCtrl * ctrl, NFASim * sim, NFA * start_state)
{
  sim_pool_append(&(ctrl->thread_pool), sim);
  return sim_pool_shift(&(ctrl->active_threads));
}


static void
load_next(NFASim * sim, NFA * nfa, SimPoolList * dst, const char * lookahead)
{
  NFASimCtrl * ctrl = sim->ctrl;

  switch(nfa->value.type) {
    case NFA_CAPTUREGRP_BEGIN: {
      sim->tracking_backrefs = 1;
      sim->backref_match[nfa->id].start = lookahead;
      sim->backref_match[nfa->id].end = NULL;
      process_adjacents(sim, nfa, dst, lookahead);
    } break;
    case NFA_CAPTUREGRP_END: {
      sim->backref_match[nfa->id].end = lookahead - 1;
      process_adjacents(sim, nfa, dst, lookahead);
    } break;
    case NFA_INTERVAL: {
      ++(sim->interval_count);
      int start, end;
      int far_end = list_size(&(nfa->reachable));
      int count = ++((sim->loop_record[nfa->id]).count);
      // check the status bit for loop count if dirty then memset it 0
      NFA * tmp = NULL;
      if(count < nfa->value.min_rep) {
        start = 1;
        end = nfa->value.split_idx;
        ++(sim->tracking_intervals);
        list_for_each(tmp, &(nfa->reachable), start, end) {
          thread_clone(tmp, sim, dst, lookahead);
        }
        tmp = list_get_head(&(nfa->reachable));
        load_next(sim, tmp, dst, lookahead);
      }
      else {
        if(nfa->value.max_rep > 0) {
          if(count < nfa->value.max_rep) {
            ++(sim->tracking_intervals);
            end = nfa->value.split_idx;
            if(CHECK_NFA_CYCLE_FLAG(nfa)) {
              if(ctrl->last_interval_pos == lookahead) {
                sim->loop_record[nfa->id].count = 0;
                --(sim->tracking_intervals);
                start = nfa->value.split_idx + 1; // list_get_at is zero based!
                list_for_each(tmp, &(nfa->reachable), start, far_end) {
                  thread_clone(tmp, sim, dst, lookahead);
                }
                tmp = list_get_at(&(nfa->reachable), nfa->value.split_idx);
                load_next(sim, tmp, dst, lookahead);
                break;
              }
              ctrl->last_interval_pos = lookahead;
            }
            start = 0;
            list_for_each(tmp, &(nfa->reachable), start, end) {
              thread_clone(tmp, sim, dst, lookahead);
            }
            sim->loop_record[nfa->id].count = 0;
            --(sim->tracking_intervals);
            start = nfa->value.split_idx + 1; // list_get_at is zero based!
            list_for_each(tmp, &(nfa->reachable), start, far_end) {
              thread_clone(tmp, sim, dst, lookahead);
            }
            tmp = list_get_at(&(nfa->reachable), nfa->value.split_idx);
            load_next(sim, tmp, dst, lookahead);
          }
          else {
            --(sim->tracking_intervals);
            sim->loop_record[nfa->id].count = 0;
            if(CHECK_NFA_CYCLE_FLAG(nfa)) {
              start = 1;
              list_for_each(tmp, &(nfa->reachable), start, far_end) {
                thread_clone(tmp, sim, dst, lookahead);
              }
              tmp = list_get_head(&(nfa->reachable));
              load_next(sim, tmp, dst, lookahead);
            }
            else {
              start = nfa->value.split_idx + 1; // list_get_at is zero based!
              list_for_each(tmp, &(nfa->reachable), start, far_end) {
                thread_clone(tmp, sim, dst, lookahead);
              }
              tmp = list_get_at(&(nfa->reachable), nfa->value.split_idx);
              load_next(sim, tmp, dst, lookahead);
            }
          }
        }
        else {
          // unbounded upper limit
          ++(sim->tracking_intervals);
          start = 0;
          end = nfa->value.split_idx;
          list_for_each(tmp, &(nfa->reachable), start, end) {
            thread_clone(tmp, sim, dst, lookahead);
          }

          if(CHECK_NFA_ACCEPTS_FLAG(nfa) == 0) {
            sim->loop_record[nfa->id].count = 0;
          }

          start = nfa->value.split_idx + 1; // list_get_at is zero based!
          list_for_each(tmp, &(nfa->reachable), start, far_end) {
            thread_clone(tmp, sim, dst, lookahead);
          }
          tmp = list_get_at(&(nfa->reachable), nfa->value.split_idx);
          load_next(sim, tmp, dst, lookahead);
        }
      }
    } break;
    case NFA_ACCEPTING: {
      sim->ip = nfa;
      sim->status = 0;
      sim_pool_append(&(ctrl->active_threads), sim);
    } break;
    default: {
      int match   =  1;
      sim->status = -1;
      if(sim->tracking_intervals || ((ctrl->active_threads_sp)[nfa->id] != lookahead)) {
        if(nfa->value.type == NFA_LITERAL) {
          match = (nfa->value.literal == *lookahead);
        }
        else if(nfa->value.type == NFA_RANGE) {
          match = is_literal_in_range(*(nfa->value.range), *lookahead);
        }
        else {
          sim->ip = nfa;
          sim->status =  0;
        }

        if(match) {
          (ctrl->active_threads_sp)[nfa->id] = lookahead;
          sim->ip = nfa;
          sim->status =  0;
          sim_pool_append(dst, sim);
        }
      }
    }
  }
}


static void
process_adjacents(NFASim *sim, NFA * nfa, SimPoolList * dst, const char * lookahead)
{
  for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
    thread_clone(list_get_at(&(nfa->reachable), i), sim, dst, lookahead);
  }
  load_next(sim, list_get_head(&(nfa->reachable)), dst, lookahead);
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
          process_adjacents(sim, sim->ip, &(ctrl->next_threads), ctrl->cur_pos + 1);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_LITERAL: {
        if(INPUT(sim) == INST_TOKEN(sim)) {
          update_match(sim);
          process_adjacents(sim, sim->ip, &(ctrl->next_threads), ctrl->cur_pos + 1);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_RANGE: {
        if(INPUT(sim) != EOL(sim)) {
          if(is_literal_in_range(RANGE(sim), INPUT(sim))) {
            update_match(sim);
            process_adjacents(sim, sim->ip, &(ctrl->next_threads), ctrl->cur_pos + 1);
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
        if(sim->bref_ptr == NULL) {
          update_match(sim);
          sim->bref_ptr = BACKREF_START(sim);
        }
        if(sim->bref_ptr) {
          if(INPUT(sim) == *(sim->bref_ptr)) {
            if(sim->bref_ptr == BACKREF_END(sim)) {
              update_match(sim);
              sim->bref_ptr = NULL;
              process_adjacents(sim, sim->ip, &(ctrl->next_threads), ctrl->cur_pos + 1);
              break;
            }
            else {
              ++(sim->bref_ptr);
              sim->status = 0;
              sim_pool_append(&(ctrl->next_threads), sim);
              break;
            }
          }
        }
        sim->status = -1;
        #undef BACKREF_START
        #undef BACKREF_END
      } break;
      case NFA_BOL_ANCHOR: {
        if(sim->ctrl->cur_pos == sim->scanner->buffer) {
          process_adjacents(sim, sim->ip, &(ctrl->active_threads), ctrl->cur_pos);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_EOL_ANCHOR: {
        if(INPUT(sim) == EOL(sim)) {
          process_adjacents(sim, sim->ip, &(ctrl->next_threads), ctrl->cur_pos);
        }
        else {
          sim->status = -1;
        }
      } break;
/* uncomment when these are fully implemented
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
*/
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
load_start_states(NFASimCtrl * ctrl, NFA * start_state, const char * lookahead)
{
  NFA * tmp = NULL;
  NFASim * clone = NULL;
  int far_end = list_size(&(start_state->reachable));

  list_for_each(tmp, &(start_state->reachable), 0, far_end) {
    if(ctrl->thread_pool.head != NULL) {
      clone = sim_pool_shift(&(ctrl->thread_pool));
      memset(clone->loop_record, 0, sizeof(LoopRecord) * ctrl->loop_record_cap);
    }
    else {
      clone = xmalloc(ctrl->size);
      clone->ctrl = ctrl;
      clone->scanner = ctrl->scanner;
    }

    clone->ip                           = tmp;
    clone->status                       = 0;
    clone->bref_ptr                     = NULL;
    clone->match.end                    = NULL;
    clone->match.start                  = NULL;
    clone->interval_count               = 0;
    clone->tracking_backrefs            = 0;
    clone->ctrl->match.last_match_end   = NULL;

    if(tmp->value.type == NFA_RANGE) {
      if(is_literal_in_range(*(tmp->value.range), *lookahead) == 0) {
        sim_pool_append(&(ctrl->thread_pool), clone);
      }
      else {
        sim_pool_append(&(ctrl->active_threads), clone);
      }
    }
    else {
      load_next(clone, tmp, &(ctrl->active_threads), lookahead);
      if(clone->status == -1) {
        sim_pool_append(&(ctrl->thread_pool), clone);
      }
    }
  }
}


static void
find_keyword_start_pos(NFASimCtrl * ctrl)
{
  void * kw_search_obj = ctrl->kw_search_obj;
  char * text_beg = (char *)ctrl->buffer_start;
  char * text_end = (char *)ctrl->buffer_end;
  ctrl->kw_search(kw_search_obj, text_beg, text_end);
  ctrl->cur_pos = ctrl->buffer_end + 1;
  if(ctrl->kw_match_count(kw_search_obj) > 0) {
    MatchRecord * mr = ctrl->kw_next_match(kw_search_obj);
    ctrl->cur_pos = mr->beg;
    load_start_states(ctrl, ctrl->start_state, ctrl->cur_pos);
  }
}

static void
update_input_pointer(NFASimCtrl * ctrl, int current_run, const char ** input_pointer)
{
  if(ctrl->kw_search_obj == NULL) {
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
      if((mr = ctrl->kw_next_match(ctrl->kw_search_obj)) != NULL) {
        (*input_pointer) = mr->beg;;
      }
      else {
        (*input_pointer) = "\0"; // force the recognizer to quit
      }
    }
    else {
      // we matched a nonempty string: current_match <-- 1 && ctrl->match.end <-- 1
      // in this case ctrl->match.end is the 'end position' of the latest match
      while((mr = ctrl->kw_next_match(ctrl->kw_search_obj)) != NULL && (mr->beg <= (ctrl->match.end)));
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
run_nfa(NFASimCtrl * ctrl)
{
  int match_found            = 0; // a match was found at some point
  int current_run            = 0; // did we match in the current run?
  ctrl->last_interval_pos    = 0;
  const char * input_pointer = ctrl->cur_pos;
  enum {NO_MATCH= -1, CONTINUE, MATCH};

  NFASim * thread = NULL;

  if(ctrl->kw_search_obj != NULL) {
    find_keyword_start_pos(ctrl);
    input_pointer = ctrl->cur_pos;
    thread = sim_pool_shift(&(ctrl->active_threads));
    if(thread == NULL) {
      // there is no way we will ever match this line
      goto RELEASE_ALL_THREADS;
    }
  }
  else {
    // don't prescan the input
    load_start_states(ctrl, ctrl->start_state, ctrl->cur_pos);
    thread = sim_pool_shift(&(ctrl->active_threads));
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
          thread = release_thread(ctrl, thread, ctrl->start_state);
        } break;
        case CONTINUE: {
          thread = sim_pool_shift(&(ctrl->active_threads));
        } break;
      }
    }

    if(ctrl->next_threads.head != NULL) {
      swap_next_w_active(ctrl);
      thread = sim_pool_shift(&(ctrl->active_threads));
      ++(ctrl->cur_pos);// = ++input_pointer;
    }
    else {
      if(LINE_OR_INVERT(ctrl) == 0) {
        // the -o command-line option was specified
        new_match(ctrl);
      }

      update_input_pointer(ctrl, current_run, &input_pointer);
      ctrl->last_interval_pos = 0;
      current_run = 0;
      ctrl->match.start = NULL;
      ctrl->match.end = NULL;
      ctrl->cur_pos = input_pointer;
      thread = reset_thread(ctrl, ctrl->start_state, (char *)input_pointer);
    }
  }
RELEASE_ALL_THREADS:
  release_all_threads(ctrl, thread);

  if(((match_found == 0) && INVERT_MATCH(ctrl))
  || ((match_found == 1) && SHOW_MATCH_LINE(ctrl))) {
    ctrl->match.start = ctrl->buffer_start;
    ctrl->match.end   = ctrl->buffer_end - 1;
    new_match(ctrl);
  }

  if(ctrl->kw_search_obj != NULL) {
    ctrl->kw_clear_matches(ctrl->kw_search_obj);
  }

  return match_found;
}


NFASimCtrl *
new_nfa_sim(Parser * parser, Scanner * scanner, ctrl_flags * cfl)
{
  NFASim * sim;
  int sz    = sizeof(*sim) + (sizeof(*(sim->loop_record)) * (parser->interval_count));
  sim       = xmalloc(sz);
  sim->ctrl = xmalloc(sizeof(*sim->ctrl) + (sizeof(char *) * parser->total_nfa_ids + 1));
  sim->ip                    = ((NFA *)peek(parser->symbol_stack))->parent;
  sim->scanner               = scanner;
  sim->ctrl->size            = sz;
  sim->ctrl->scanner         = scanner;
  sim->ctrl->loop_record_cap = parser->interval_count;
  sim->ctrl->ctrl_flags      = cfl;
  sim->ctrl->start_state     = sim->ip;

  sim->ctrl->thread_pool.head     = sim;
  sim->ctrl->thread_pool.tail     = sim;
  sim->ctrl->active_threads.head  = NULL;
  sim->ctrl->active_threads.tail  = NULL;
  sim->ctrl->next_threads.head    = NULL;
  sim->ctrl->next_threads.tail    = NULL;

  NFASimCtrl * ctrl = sim->ctrl;;
  int kw_count = list_size(parser->synth_patterns);
  if(kw_count != 0) {
    if(kw_count == 1) {
      ctrl->kw_search_obj = new_bm_obj();
      const char * kw = list_get_head(parser->synth_patterns);
      bm_init_obj(ctrl->kw_search_obj, kw, strlen(kw));
      ctrl->kw_search = bm_search;
      ctrl->kw_clear_matches = bm_clear_matches;
      ctrl->kw_free_search_obj = bm_obj_free;
      ctrl->kw_next_match = bm_next_match;
      ctrl->kw_match_count = bm_match_count;
    }
    else {
      ctrl->kw_search_obj = new_mpat();
      if(mpat_init(ctrl->kw_search_obj, parser->synth_patterns) == 0) {
        mpat_obj_free((MPatObj **)&(ctrl->kw_search_obj));
      }
      else {
        ctrl->kw_search = mpat_search;
        ctrl->kw_clear_matches = mpat_clear_matches;
        ctrl->kw_free_search_obj = mpat_obj_free;
        ctrl->kw_next_match = mpat_next_match;
        ctrl->kw_match_count = mpat_match_count;
      }
    }
  }

  return sim->ctrl;
}


void
reset_nfa_sim(NFASimCtrl * ctrl)
{
  ctrl->match.last_match_end = NULL;
  ctrl->buffer_start         = get_cur_pos(ctrl->scanner);
  ctrl->buffer_end           = get_buffer_end(ctrl->scanner) - 1;
  ctrl->filename             = get_filename(ctrl->scanner);
  ctrl->filename_len         = strlen(ctrl->filename);
  ctrl->cur_pos              = ctrl->buffer_start;
  ctrl->match.start          = NULL;
  ctrl->match.end            = NULL;
  return;
}


void
free_nfa_sim(NFASimCtrl * ctrl)
{
  if(ctrl == NULL) {
    return;
  }

  release_all_threads(ctrl, NULL);

  if(ctrl->thread_pool.head != NULL) {
    NFASim * tmp = NULL;
    while((tmp = sim_pool_shift(&(ctrl->thread_pool))) != NULL) {
      free(tmp);
    }
  }

  if(ctrl->kw_search_obj) {
    ctrl->kw_free_search_obj(&(ctrl->kw_search_obj));
  }
  free(ctrl);
}
