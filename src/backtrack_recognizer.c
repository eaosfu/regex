#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "bits.h"
#include "slist.h"
#include "token.h"
#include "misc.h"
#include "nfa.h"
#include "scanner.h"
#include <stddef.h>
#include "backtrack_recognizer.h"
// FIXME: Put this inside the sim->ctrl, so it can be freed when we're done
typedef const char * active_thread_input_ptr;
static active_thread_input_ptr * active_threads_sp;

#define EOL(sim)               ((sim)->scanner->eol_symbol)
#define RANGE(sim)             (*((sim)->ip->value.range))
#define INPUT(sim)             (*((sim)->input_ptr))
#define INST_TYPE(sim)         ((sim)->ip->value.type)
#define INST_TOKEN(sim)        ((sim)->ip->value.literal)
#define INST_LONG_TOKEN(sim)   ((sim)->ip->value.lliteral[(sim)->ip->value.idx])
#define RELEASE_ALL_THREADS(t) while((t)) { (t) = release_thread((t), (t)->ctrl->start_state); }

static int  load_next(NFASim *, NFA *);
static int  process_adjacents(NFASim *sim, NFA * nfa);


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
    sim->match.start = sim->input_ptr;
    sim->match.end   = sim->input_ptr;
  }
  else {
    sim->match.end = sim->input_ptr;
  }
}


static Match *
//new_match(NFASimCtrl * ctrl, const char * filename, int nm_len)
new_match(NFASimCtrl * ctrl, int shorten)
{
  Match * match = NULL;
  if(((CTRL_FLAGS(ctrl) & SILENT_MATCH_FLAG) == 0)) {
    const char * filename = ctrl->filename;
    const char * begin    = ctrl->match.start;
    const char * end      = ctrl->match.end;
    int nm_len            = ctrl->filename_len;
    int line_no           = ctrl->scanner->line_no;
    int w = 1;

    if(begin && (begin <= end)) {
      int sz = end - begin + 1;

      if((CTRL_FLAGS(ctrl) & SHOW_FILE_NAME_FLAG) && nm_len > 0) {
        sz += nm_len + 1; // add ':' after <filename>
      }

      if((CTRL_FLAGS(ctrl) & SHOW_LINENO_FLAG)) {
        int tmp = line_no;
        while(tmp/10 > 0 && (tmp /=10) && ++w);
        sz += w + 1; // add ':' after <line number>
      }

      // +1 for the '\0'
      if((ctrl->match_idx + sz + 1) >= MATCH_BUCKET_SIZE) {
        flush_matches(ctrl);
      }

      if(ctrl->match.last_match_end) {
        if((ctrl->match.start - ctrl->match.last_match_end) == 0) {
          ctrl->match_idx -= 2;
        }
        if((ctrl->match.start - ctrl->match.last_match_end) == 1) {
          ctrl->match_idx -= 1;
        }
      }


      if((CTRL_FLAGS(ctrl) & SHOW_FILE_NAME_FLAG) && nm_len > 0) {
        snprintf(ctrl->matches + ctrl->match_idx, nm_len + 2, "%s:", filename);
        ctrl->match_idx += nm_len + 1;
        sz -= nm_len + 1;
      }

      if((CTRL_FLAGS(ctrl) & SHOW_LINENO_FLAG)) {
        snprintf(ctrl->matches + ctrl->match_idx, nm_len + w + 1, "%d:", line_no);
        ctrl->match_idx += w + 1;
        sz -= w + 1;
      }

      if(shorten) {
        --sz;
      }

      strncpy((ctrl->matches) + ctrl->match_idx, begin, sz);
      ctrl->match_idx += sz;
      ctrl->matches[ctrl->match_idx] = '\n';
      ++(ctrl->match_idx);
      ctrl->matches[ctrl->match_idx] = '\0';
      ctrl->match.last_match_end = ctrl->match.end;
    }
  }
  return match;
}


inline void
flush_matches(NFASimCtrl * ctrl)
{
  if(((CTRL_FLAGS(ctrl) & SILENT_MATCH_FLAG) == 0)) {
    printf("%s", ctrl->matches);
    ctrl->match_idx = 0;
    ctrl->match.last_match_end = 0;
  }
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
is_literal_in_range(nfa_range range, int c)
{
  int ret = 0;
  if(c > 0) {
    unsigned int mask = set_bit(RANGE_BITVEC_WIDTH, c)|0;
    if(range[get_bit_array_idx(c, RANGE_BITVEC_WIDTH)] & mask) {
      ret = 1;
    }
  }
  return ret;
}


static NFASim *
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
    process_adjacents(sim, start_state);
  }
  else {
    fatal("Unable to obtain an execution threads\n");
  }
  return sim;
}


static inline NFASim *
thread_clone(NFASim * sim, NFA * nfa, int id)
{
  NFASim * clone;
  NFASimCtrl * ctrl = sim->ctrl;

  int new_thread = 0;

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
  else if(new_thread == 0) {
    memset(clone->loop_record, 0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
  }

  if(clone->tracking_backrefs > 0 && new_thread == 0) {
    memcpy(clone->backref_match, sim->backref_match, sizeof(Match) * MAX_BACKREF_COUNT);
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
  sim->ip                 = start_state;
  sim->status             = 0;
  sim->match.end          = NULL;
  sim->match.start        = NULL;
  sim->interval_count     = 0;
  sim->tracking_intervals = 0;
  sim->tracking_backrefs  = 0;
  list_push(sim->ctrl->thread_pool, sim);
  return list_shift(sim->ctrl->active_threads);
}


static int 
load_next(NFASim * sim, NFA * nfa)
{
  switch(nfa->value.type) {
    case NFA_SPLIT: {
      for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
        thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
      }
      load_next(sim, list_get_at(&(nfa->reachable), 0));
    } break;
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
      int count = ++((sim->loop_record[nfa->id]).count);
      if(count < nfa->value.min_rep) {
        ++(sim->tracking_intervals);
        for(int i = 1; i < nfa->value.split_idx; ++i) {
          thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
        }
        load_next(sim, list_get_at(&(nfa->reachable), 0));
      }
      else {
        if(nfa->value.max_rep > 0) {
          if(count < nfa->value.max_rep) {
            ++(sim->tracking_intervals);
            for(int i = 0; i < nfa->value.split_idx; ++i) {
              thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
            }

            sim->loop_record[nfa->id].count = 0;

            --(sim->tracking_intervals);
            for(int i = nfa->value.split_idx + 1; i < list_size(&(nfa->reachable)); ++i) {
              thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
            }
            load_next(sim, list_get_at(&(nfa->reachable), nfa->value.split_idx));
          }
          else {
            --(sim->tracking_intervals);

            sim->loop_record[nfa->id].count = 0;

            if(nfa->full_circle) {
              for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
                thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
              }
              load_next(sim, list_get_at(&(nfa->reachable), 0));
            }
            else {
              for(int i = nfa->value.split_idx + 1; i < list_size(&(nfa->reachable)); ++i) {
                thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
              }
              load_next(sim, list_get_at(&(nfa->reachable), nfa->value.split_idx));
            }

          }
        }
        else {
          // unbounded upper limit
            ++(sim->tracking_intervals);
            for(int i = 0; i < nfa->value.split_idx; ++i) {
              thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
            }

            if(nfa->reaches_accept == 0) {
              sim->loop_record[nfa->id].count = 0;
            }

            for(int i = nfa->value.split_idx + 1; i < list_size(&(nfa->reachable)); ++i) {
              thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
            }
            load_next(sim, list_get_at(&(nfa->reachable), nfa->value.split_idx));
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
      if(sim->tracking_intervals || (active_threads_sp[nfa->id] != sim->input_ptr)) {
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
          return 1;
        }

        if(match) {
          active_threads_sp[nfa->id] = sim->input_ptr;
          sim->ip = nfa;
          sim->status =  0;
        }
      }
    }
  }
}


static int inline
process_adjacents(NFASim *sim, NFA * nfa)
{
  for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
    thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
  }
  load_next(sim, list_get_at(&(nfa->reachable), 0));
}



static inline int
thread_step(NFASim * sim)
{
  active_threads_sp[sim->ip->id] = NULL;
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
        if(match && sim->ip->value.idx == 0) {
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
        if(bref_end) {
          int fail = 0;
          const char * tmp_input = sim->input_ptr;
          const char * bref_ptr = BACKREF_START(sim);
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
      case NFA_ACCEPTING: {
        sim->status = 1;
      } break;
      default: { // should never hit this condition
        sim->status = -1;
      }
    }
  }
  else {
    sim->status = 1;
  }

  return sim->status;
}


int
run_nfa(NFASim * thread)
{
  int match_found            = 0;
  int current_run            = 0; // did we match in the current run?
  NFASimCtrl * ctrl          = thread->ctrl;
  thread->input_ptr          = ctrl->buffer_start;
  const char * buffer_end    = get_buffer_end(thread->scanner) - 1;
  const char * input_pointer = ctrl->buffer_start;
  
  process_adjacents(thread, ctrl->start_state);

  while((*input_pointer) != '\0') {
    while(thread) {
      thread_step(thread);
      switch(thread->status) {
        case 1: {
          current_run = match_found = 1;
          if((CTRL_FLAGS(ctrl) & (SILENT_MATCH_FLAG|INVERT_MATCH_FLAG)) == 0) {
            update_longest_match(&(ctrl->match), &(thread->match));
            if(((CTRL_FLAGS(ctrl) & MGLOBAL_FLAG) == 0)) {
              new_match(ctrl, 0);
              goto RELEASE_ALL_THREADS;
            }
          }
          else if((CTRL_FLAGS(ctrl) & INVERT_MATCH_FLAG) || (CTRL_FLAGS(ctrl) & MGLOBAL_FLAG) == 0) {
            goto RELEASE_ALL_THREADS;
          }
        } // fall through
        case -1: {
          // kill thread
          thread = release_thread(thread, ctrl->start_state);
        } break;
        case 0: {
          // keep trying
          if(list_size(ctrl->active_threads) > 0) {
            list_append(ctrl->active_threads, thread);
            thread = list_shift(ctrl->active_threads);
          }
        } break;
      }
    }

    if((CTRL_FLAGS(ctrl) & INVERT_MATCH_FLAG) == 0) {
      new_match(ctrl, 0);
    }
    if((current_run == 0) || (ctrl->match.end == 0)) {
      ++input_pointer;
    }
    else {
      input_pointer = ctrl->match.end + 1;
    }
    current_run = 0;
    ctrl->match.start = NULL;
    ctrl->match.end = NULL;
    thread = reset_thread(ctrl, ctrl->start_state, (char *)input_pointer);
  }
RELEASE_ALL_THREADS:
  RELEASE_ALL_THREADS(thread);

  if((match_found == 0)
  && (CTRL_FLAGS(ctrl) & INVERT_MATCH_FLAG)
  && ((CTRL_FLAGS(ctrl) & SILENT_MATCH_FLAG) == 0)) {
    ctrl->match.start = ctrl->buffer_start;
    ctrl->match.end   = ctrl->buffer_end;
    new_match(ctrl, 1);
  }
  return match_found;
}


NFASim *
new_nfa_sim(Parser * parser, Scanner * scanner, ctrl_flags * cfl)
{
  NFASim * sim;
  int sz    = sizeof(*sim) + (sizeof(*(sim->loop_record)) * (parser->interval_count));
  sim       = xmalloc(sz);
  sim->size = sz;
  sim->ctrl = xmalloc(sizeof(*(sim->ctrl)));
  active_threads_sp = xmalloc(sizeof(char *) * (parser->total_nfa_ids + 1));

  sim->ip                    = peek(parser->symbol_stack);
  sim->scanner               = scanner;
  sim->ctrl->scanner         = scanner;
  sim->ctrl->loop_record_cap = parser->interval_count;
  sim->ctrl->active_threads  = new_list();
  sim->ctrl->thread_pool     = new_list();
  sim->ctrl->ctrl_flags      = cfl;
  sim->ctrl->start_state     = sim->ip;
  sim->ctrl->filename        = get_filename(scanner);
  sim->ctrl->filename_len    = strlen(sim->ctrl->filename);
  return sim;
}


void
reset_nfa_sim(NFASim * sim, NFA * start_state)
{
  sim->match.end                  = NULL;
  sim->match.start                = NULL;
  sim->ctrl->start_state          = start_state;
  sim->ctrl->match.last_match_end = NULL;
  sim->ctrl->buffer_start         = get_cur_pos(sim->scanner);
  sim->ctrl->buffer_end           = get_buffer_end(sim->scanner) - 1;
  memset(sim->loop_record,   0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
}


void
free_nfa_sim(NFASim * nfa_sim)
{
  list_iterate(((*(NFASimCtrl **)nfa_sim)->active_threads), (void *)&free);
  list_free(((*(NFASimCtrl **)nfa_sim)->active_threads), NULL);

  list_iterate(((*(NFASimCtrl **)nfa_sim)->thread_pool), (void *)&free);
  list_free(((*(NFASimCtrl **)nfa_sim)->thread_pool), NULL);

  free(nfa_sim->ctrl);
  free(nfa_sim);
}
