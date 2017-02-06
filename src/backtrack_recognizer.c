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

int match_interval_count1 = 0;
int match_interval_count2 = 0;
// FIXME: Put this inside the sim->ctrl, so it can be freed when we're done
typedef const char * active_thread_input_ptr;
static active_thread_input_ptr * active_threads_sp;

#define INPUT(sim) (*((sim)->input_ptr))
#define EOL(sim)  ((sim)->scanner->eol_symbol)
#define NEXT_INST(sim) ((sim)->ip->out2)
#define CUR_INST(sim) ((sim)->ip)
#define RANGE(sim) (*((sim)->ip->value.range))
#define INST_TYPE(sim) ((sim)->ip->value.type)
#define INST_TOKEN(sim) ((sim)->ip->value.literal)
#define INST_LONG_TOKEN(sim) ((sim)->ip->value.lliteral[(sim)->ip->value.idx])
#define RELEASE_ALL_THREADS(t) while((t)) { (t) = release_thread((t), start_state); }
#define SKIP_EPSILONS(nfa) ({ while((nfa->value.type == NFA_EPSILON)) { \
                                (nfa) = (nfa)->out2; } })

#define SKIP_EPSILONS_AND_CAPTUREGRP_END(nfa) ({ \
  while((nfa->value.type == NFA_EPSILON || nfa->value.type == NFA_CAPTUREGRP_END)) { (nfa) = (nfa)->out2; } \
})

static int  load_next(NFASim *, NFA *);
static int  process_adjacents(NFASim *sim, NFA * nfa);
static void get_start_state(NFASim * sim, NFA * nfa);


void
update_longest_match(Match * m, Match * nm)
{
  if(m->end == 0 || ((m->end - m->start) < (nm->end - nm->start))) {
    m->start = nm->start;
    m->end = nm->end;
  }
  else if(((nm->start - m->end) > -1) && ((nm->start - m->end) <= 1)) {
    m->end = nm->end;
  }
}


void
update_match(NFASim * sim)
{
  if(sim->match.start == 0) {
    sim->match.start = sim->input_ptr;
    sim->match.end = sim->input_ptr;
  }
  else {
    sim->match.end = sim->input_ptr;
  }
}


Match *
new_match(NFASimCtrl * ctrl, const char * filename, int nm_len)
{
  Match * match = NULL;
  if(((CTRL_FLAGS(ctrl) & SILENT_MATCH_FLAG) == 0)) {
    const char * begin = ctrl->match.start;
    const char * end = ctrl->match.end;
    if(begin && (begin <= end)) {
      int sz = end - begin + 1;

      if((CTRL_FLAGS(ctrl) & SHOW_FILE_NAME_FLAG) && nm_len > 0) {
        sz += nm_len + 1; // add ':' after <filename>
      }

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


static inline int
is_literal_in_range(nfa_range range, unsigned int c)
{
  int ret = 0;
  unsigned int mask = set_bit(RANGE_BITVEC_WIDTH, c)|0;
  if(range[get_bit_array_idx(c, RANGE_BITVEC_WIDTH)] & mask) {
    ret = 1;
  }
  return ret;
}


void *
free_match_string(void * m)
{
  if(m) {
    free(m);
  }
  return ((void *)NULL);
}


void
record_match(NFASim * sim, char * begin, char * end)
{
  sim->match.start = begin;
  sim->match.end = end;
}


NFASim *
reset_thread(NFASimCtrl * ctrl, NFA * start_node, char * start_pos)
{
  NFASim * sim = NULL;
  if(list_size(ctrl->thread_pool)) {
    sim = list_shift(ctrl->thread_pool);
    reset_nfa_sim(sim, start_node);
    restart_from(sim->scanner, start_pos);
    sim->input_ptr = start_pos;
    sim->tracking_backrefs  = 0;
    memset(sim->loop_record, 0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
    //load_next(sim, start_node);
    process_adjacents(sim, start_node);
    //get_start_state(sim, start_node);
  }
  else {
    fatal("Unable to obtain an execution threads\n");
  }
  return sim;
}

/*
static inline NFASim *
thread_clone(NFASim * sim, NFA * nfa, int id)
{
  NFASim * clone;
  NFASimCtrl * ctrl = sim->ctrl;

//if(nfa->value.type == NFA_SPLIT) {
//  printf("SPLIT SHIT!\n");
//}
//if( nfa->value.type == NFA_TREE) {
//  printf("TREE SHIT!\n");
//}
//if(nfa->value.type == NFA_INTERVAL) {
//  printf("INTERVAL SHIT!\n");
//}


  if(list_size(ctrl->thread_pool)) {
    clone = list_shift(ctrl->thread_pool);
  }
  else {
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
  else {
    memset(clone->loop_record, 0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
  }

  if(clone->tracking_backrefs > 0) {
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
*/

static inline NFASim *
thread_clone(NFASim * sim, NFA * nfa, int id)
{
  NFASim * clone;
  NFASimCtrl * ctrl = sim->ctrl;
/*
if(nfa->value.type == NFA_SPLIT) {
  printf("SPLIT SHIT!\n");
}
if( nfa->value.type == NFA_TREE) {
  printf("TREE SHIT!\n");
}
if(nfa->value.type == NFA_INTERVAL) {
  printf("INTERVAL SHIT!\n");
}
*/

  if(list_size(ctrl->thread_pool)) {
    clone = list_shift(ctrl->thread_pool);
  }
  else {
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
  else {
    memset(clone->loop_record, 0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
  }

  if(clone->tracking_backrefs > 0) {
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
  list_push(sim->ctrl->thread_pool, sim);
  active_threads_sp[sim->ip->id] = NULL;
  sim->ip = start_state;
  sim->status = 0;
  sim->interval_count = 0;
  sim->match.start = NULL;
  sim->match.end = NULL;
  sim->tracking_intervals = 0;
  sim->tracking_backrefs = 0;
  return list_shift(sim->ctrl->active_threads);
}


NFASim *
new_nfa_sim(Parser * parser, Scanner * scanner, ctrl_flags * cfl)
{
  NFASim * sim;
  int sz = sizeof(*sim) + (sizeof(*(sim->loop_record)) * (parser->loops_to_track));
  sim = xmalloc(sz);
  sim->size = sz;
  sim->ctrl = xmalloc(sizeof(*(sim->ctrl)));

  active_threads_sp = xmalloc(sizeof(char *) * (parser->total_nfa_ids + 1));

  sim->ctrl->loop_record_cap = parser->loops_to_track;
  sim->ctrl->active_threads = new_list();
  sim->ctrl->thread_pool = new_list();
  sim->ctrl->ctrl_flags  = cfl;

  sim->scanner = scanner;
  sim->ip = peek(parser->symbol_stack);
  return sim;
}


void
reset_nfa_sim(NFASim * sim, NFA * start_state)
{
  sim->ip = start_state;
  sim->match.start = sim->match.end = NULL;
  sim->ctrl->match.last_match_end = NULL;
  memset(sim->loop_record,   0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
}


int
load_next(NFASim * sim, NFA * nfa)
{
//printf("load_next(): nfa->value.type: %d\n", nfa->value.type);
  switch(nfa->value.type) {
    case NFA_SPLIT: {
      for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
        thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
      }
      load_next(sim, list_get_at(&(nfa->reachable), 0));
    } break;
    case NFA_CAPTUREGRP_BEGIN: {
//printf("CAPTURE GROUP BEGIN: %d\n", nfa->id);
      sim->tracking_backrefs = 1;
      sim->backref_match[nfa->id].start = sim->input_ptr;
      sim->backref_match[nfa->id].end = NULL;
      process_adjacents(sim, nfa);
    } break;
    case NFA_CAPTUREGRP_END: {
//printf("CAPTURE GROUP END: %d\n", nfa->id);
      sim->backref_match[nfa->id].end = sim->input_ptr - 1;
      process_adjacents(sim, nfa);
    } break;
    case NFA_INTERVAL: {
      ++(sim->interval_count);
      int count = ++((sim->loop_record[nfa->id]).count);
      NFASim * clone = NULL;
      NFA * tmp = NULL;
      if(nfa->value.min_rep > 0 && count < nfa->value.min_rep) {
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

            if(nfa->reaches_accept == 0) {
              sim->loop_record[nfa->id].count = 0;
            }

            --(sim->tracking_intervals);
            for(int i = nfa->value.split_idx + 1; i < list_size(&(nfa->reachable)); ++i) {
              thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
            }
            load_next(sim, list_get_at(&(nfa->reachable), nfa->value.split_idx));
          }
          else {
            --(sim->tracking_intervals);

            if(nfa->reaches_accept == 0) {
              sim->loop_record[nfa->id].count = 0;
            }

            for(int i = nfa->value.split_idx + 1; i < list_size(&(nfa->reachable)); ++i) {
              thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
            }
            load_next(sim, list_get_at(&(nfa->reachable), nfa->value.split_idx));
          }
        }
        else {
          // unbounded upper limit
          // FIXME:  this is wrong
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
/*
int x = 15;
int d = 0;
if(sim->match.start) {
  const char * s = sim->match.start;
  while(s <= sim->match.end) {
    ++d;
    printf("%c", *s);
    ++s;
  }

  for(int i = 0; i < (x-d) - 1; ++i) {
    printf(" ");
  }

  printf(" -- [%d] [%d] [%d] [%d] -- tc(%d)/ic(%d) -- %s ***\n",
    sim->loop_record[0].count,
    sim->loop_record[1].count,
    sim->loop_record[2].count,
    sim->loop_record[3].count,
    sim->tracking_intervals,
    sim->interval_count,
    sim->input_ptr
  );
}
*/
      sim->ip = nfa;
      sim->status = 1;
    } break;
    default: {
      //if(sim->tracking_intervals == 0 && active_threads_sp[nfa->id] == sim->input_ptr) {

      // Need this check since we can have threads that look identical in terms of
      // input_ptr, interval_record and ip but end up taking different paths on the next
      // step.
      if(sim->tracking_intervals == 0 && active_threads_sp[nfa->id] == sim->input_ptr) {
         sim->status = -1;
       }
       else {
         if(nfa->value.type == NFA_LITERAL) {
           if(nfa->value.literal == *(sim->input_ptr)) {
             sim->status = 0;
             active_threads_sp[nfa->id] = sim->input_ptr;
             sim->ip = nfa;
             sim->status =  0;
/*
  int x = 15;
  int d = 0;
  if(sim->match.start) {
    const char * s = sim->match.start;
    while(s <= sim->match.end) {
      ++d;
      printf("%c", *s);
      ++s;
    }
    for(int i = 0; i < (x-d) - 1; ++i) {
      printf(" ");
    }
    printf(" -- [%d] [%d] [%d] [%d] -- tc(%d)/ic(%d) -- %s --> %c [0x%x]\n",
      sim->loop_record[0].count,
      sim->loop_record[1].count,
      sim->loop_record[2].count,
      sim->loop_record[3].count,
      sim->tracking_intervals,
      sim->interval_count,
      sim->input_ptr,
      nfa->value.literal,
      nfa
    );
  }
*/
           }
           else {
             sim->status =  -1;
           }
         }
         else {
           sim->status = 0;
           active_threads_sp[nfa->id] = sim->input_ptr;
           sim->ip = nfa;
           sim->status =  0;
         }
         //sim->status = (nfa->value.type == NFA_ACCEPTING) ? 1 : 0;
       }
    }
  }
}


int
process_adjacents(NFASim *sim, NFA * nfa)
{
  NFA * next = NULL;
  for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
    thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
  }
  load_next(sim, list_get_at(&(nfa->reachable), 0));
}



static inline int
thread_step(NFASim * sim)
{
  if(INPUT(sim) != '\0') {
    switch(INST_TYPE(sim)) {
      case NFA_ANY: {
        if(INPUT(sim) != EOL(sim)) {
          update_match(sim);
          ++(sim->input_ptr);
active_threads_sp[sim->ip->id] = NULL;
          //load_next(sim, NEXT_INST(sim));
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_LITERAL: {
        if(INPUT(sim) == INST_TOKEN(sim)) {
//printf("[0x%x]: match: %c -- interval count: %d\n", sim, *(sim->input_ptr), sim->loop_record[0].count);
          update_match(sim);
          ++(sim->input_ptr);
active_threads_sp[sim->ip->id] = NULL;
          //load_next(sim, NEXT_INST(sim));
          process_adjacents(sim, sim->ip);
        }
        else {
active_threads_sp[sim->ip->id] = NULL;
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
active_threads_sp[sim->ip->id] = NULL;
          //load_next(sim, NEXT_INST(sim));
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->ip->value.idx = 0;
          sim->status = -1;
        }
active_threads_sp[sim->ip->id] = NULL;
      } break;
      case NFA_RANGE: {
        if(INPUT(sim) != EOL(sim)) {
          if(is_literal_in_range(RANGE(sim), INPUT(sim))) {
            update_match(sim);
            ++(sim->input_ptr);
active_threads_sp[sim->ip->id] = NULL;
            //load_next(sim, NEXT_INST(sim));
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
        // FIXME: id - 1 should just be id
        //#define BACKREF_START(sim) ((sim)->backref_match[(sim)->ip->id - 1].start)
        //#define BACKREF_END(sim) ((sim)->backref_match[(sim)->ip->id - 1].end)
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
active_threads_sp[sim->ip->id] = NULL;
            //load_next(sim, NEXT_INST(sim));
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
active_threads_sp[sim->ip->id] = NULL;
          //load_next(sim, NEXT_INST(sim));
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_EOL_ANCHOR: {
        if(INPUT(sim) == EOL(sim)) {
active_threads_sp[sim->ip->id] = NULL;
          //load_next(sim, NEXT_INST(sim));
          process_adjacents(sim, sim->ip);
        }
        else {
          sim->status = -1;
        }
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
/*
  if(sim->status == -1) {
    backtrack(sim);
  }
*/
//printf("\n");
  return sim->status;
}


void
get_start_state(NFASim * sim, NFA * nfa)
{
/*
  if(nfa->value.type == NFA_EPSILON || nfa->value.type == NFA_TREE
  || nfa->value.type == NFA_SPLIT || nfa->value.type == (NFA_SPLIT|NFA_PROGRESS)) {
    process_adjacents(sim, nfa);
  }
  else if(nfa->value.type == NFA_CAPTUREGRP_BEGIN || nfa->value.type == NFA_INTERVAL) {
    load_next(sim, nfa);
  }
  else {
    sim->ip = nfa;
    return;
  }
*/
  process_adjacents(sim, nfa);
}


int
run_nfa(NFASim * thread)
{
//printf("-- %s\n", thread->scanner->buffer);
  NFASimCtrl * ctrl = thread->ctrl;
  ctrl->match.start = NULL;
  ctrl->match.end = NULL;
  const char * buffer_start;
  const char * buffer_end;
  const char * input_pointer;
  const char * filename = get_filename(thread->scanner);
  int filename_len = strlen(filename);
  buffer_start = input_pointer = get_cur_pos(thread->scanner);
  buffer_end = get_buffer_end(thread->scanner) - 1;
  thread->input_ptr = input_pointer;
  NFA * start_state = thread->ip;
  get_start_state(thread, thread->ip);
  int match_found = 0;
  int current_run = 0; // did we match in the current run?
  Match * m = NULL;
  while((*input_pointer) != '\0') {
match_interval_count1 = match_interval_count2 = 0;
    while(thread) {
      thread_step(thread);
      switch(thread->status) {
        case 1: {
//printf("-- DONE -- interval  count1: %d -- interval count2: %d\n", match_interval_count2, match_interval_count2);
//if(thread->tracking_intervals > match_interval_count
//&& (ctrl->match.end == NULL || thread->match.end >= ctrl->match.end)) {
//  match_interval_count = thread->tracking_intervals;

/*
if(thread->interval_count > match_interval_count2) {
  match_interval_count1 = match_interval_count2;
  match_interval_count2 = thread->interval_count;
  printf("\tinterval count2 set to : %d\n", match_interval_count2);
}
*/
          current_run = match_found = 1;
          if((CTRL_FLAGS(ctrl) & (SILENT_MATCH_FLAG|INVERT_MATCH_FLAG)) == 0) {
            update_longest_match(&(ctrl->match), &(thread->match));
            if(((CTRL_FLAGS(ctrl) & MGLOBAL_FLAG) == 0)) {
              new_match(ctrl, filename, filename_len);
              goto RELEASE_ALL_THREADS;
            }
          }
          else if((CTRL_FLAGS(ctrl) & INVERT_MATCH_FLAG) || (CTRL_FLAGS(ctrl) & MGLOBAL_FLAG) == 0) {
            goto RELEASE_ALL_THREADS;
          }
/*
printf("match: ");
const char * s = thread->match.start;
while(s <= thread->match.end) {
  printf("%c", *s);
  ++s;
}
printf(" -- [%d] [%d] [%d] [%d] -- %d\n",
  thread->loop_record[0].count,
  thread->loop_record[1].count,
  thread->loop_record[2].count,
  thread->loop_record[3].count,
  thread->interval_count
);
*/

        } // fall through
        case -1: {
          // kill thread
//printf("[0x%x]: rejecting: %c vs. %c\n", thread, thread->ip->value.literal, *thread->input_ptr);
          thread = release_thread(thread, start_state);

///*

while(thread && thread->interval_count ) {
  if(thread->interval_count < match_interval_count1
  && thread->match.end < ctrl->match.end - 1) {
//  if(thread->interval_count < match_interval_count - 1
//  && thread->match.end < ctrl->match.end - 1) {
//  if(thread->interval_count <= match_interval_count
//  && thread->match.end < ctrl->match.end - 1) {
///*
//printf("BOO\n");
//    printf("[0x%x]: match interval: %d .vs interval_count: %d: ",
//      thread, match_interval_count, thread->interval_count);
//    if(thread->match.end) {
//      const char * s = thread->match.start;
//      while(s <= thread->match.end) {
//        printf("%c", *s);
//        ++s;
//      }
//      printf("\n");
//    }
//
//printf("skip\n");
    thread = release_thread(thread, start_state);
  }
  else {
    break;
  }
}
//*/
        } break;
        case 0: {
          // keep trying

//if(match_interval_count == 0) {
          if(list_size(ctrl->active_threads) > 0) {
            list_append(ctrl->active_threads, thread);
            thread = list_shift(ctrl->active_threads);
          }
//}


        } break;
      }
//printf("active threads: %d\n", list_size((ctrl->active_threads)));
    }

//    match_interval_count = 0;
    if((CTRL_FLAGS(ctrl) & (SILENT_MATCH_FLAG|INVERT_MATCH_FLAG)) == 0) {
      new_match(ctrl, filename, filename_len);
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
    thread = reset_thread(ctrl, start_state, (char *)input_pointer);
  }
RELEASE_ALL_THREADS:
  RELEASE_ALL_THREADS(thread);

  if((match_found == 0)
  && (CTRL_FLAGS(ctrl) & INVERT_MATCH_FLAG)
  && ((CTRL_FLAGS(ctrl) & SILENT_MATCH_FLAG) == 0)) {
    ctrl->match.start = buffer_start;
    ctrl->match.end   = buffer_end;
    new_match(ctrl, filename, filename_len);
  }
//flush_matches(ctrl);
//ctrl->matches[0] = '\0';
//printf("-- DONE --\n");
  return match_found;
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
