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

//void push_backtrack_stack(NFASim * sim, NFA * nfa);
int  pop_backtrack_stack(NFASim * sim);
int  load_next(NFASim *, NFA *);


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
    load_next(sim, start_node);
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
  
  memcpy(clone->loop_record, sim->loop_record, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);

  if(id >= 0 && id < ctrl->loop_record_cap) {
    clone->loop_record[id].count = 0;
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
  sim->match.start = 0;
  sim->match.end = 0;
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
//  sim->prev_thread = sim->next_thread = sim;

//  sim->tracked_loop_count = parser->loops_to_track;
  sim->scanner = scanner;
  sim->ip = peek(parser->symbol_stack);
//  sim->sp = -1;
  return sim;
}


void
reset_nfa_sim(NFASim * sim, NFA * start_state)
{
//  sim->sp = -1;
  sim->ip = start_state; 
  sim->match.start = sim->match.end = NULL;
//  sim->next_thread = sim->prev_thread = sim;
  sim->ctrl->match.last_match_end = NULL;
  //memset(sim->loop_record,   0, sizeof(LoopRecord) * sim->tracked_loop_count);
  memset(sim->loop_record,   0, sizeof(LoopRecord) * sim->ctrl->loop_record_cap);
}



void *
compare(void * a, void * b)
{
  if(a == b) {
    return a;
  }
  return NULL;
}


void
collect_reachable(NFA * root, NFA * parent, NFA * child)
{
  NFA * target = child;


  if(parent == NULL) {
    parent = root;
  }
  if(child == NULL) {
    target = root;
  }

  SKIP_EPSILONS(target);
  NFA * tmp = NULL;

  switch(target->value.type) {

    case NFA_SPLIT|NFA_PROGRESS: {
      parent = target;
      tmp = target->out1;
      SKIP_EPSILONS(tmp);
      while(tmp->value.type == NFA_EPSILON || tmp->value.literal == '?') {
        if(tmp->value.literal == '?') {
          parent = target;
          tmp = tmp->out2;
          SKIP_EPSILONS(tmp);
        }
      }
      collect_reachable(root, parent, tmp);
      collect_reachable(root, target, target->out2);
    } break;

    case NFA_SPLIT: {
      collect_reachable(root, target, target->out1);
      collect_reachable(root, target, target->out2);
    } break;
    case NFA_INTERVAL: {
      if(parent == root  && parent == target) {
        collect_reachable(root, target, target->out1);
        tmp = target->out2;
        SKIP_EPSILONS(tmp);
        if(tmp->value.type == NFA_INTERVAL) {
          list_append(&(root->reachable), tmp);
        }
        else {
          collect_reachable(root, target, target->out2);
        }
      }
      else {
        if(list_search(&(root->reachable), target, compare) == NULL) {
          list_append(&(root->reachable), target);
        }
      }
    } break;
    default: {
      if(list_search(&(root->reachable), target, compare) == NULL) {
        list_append(&(root->reachable), target);
      }
    }
  }
}


int
load_next(NFASim * sim, NFA * nfa)
{
  static int depth = 0;
  SKIP_EPSILONS(nfa);
  NFASimCtrl * ctrl = sim->ctrl;


  if(nfa->value.type != NFA_ACCEPTING
  && list_size(&(nfa->reachable)) == 0) {
    collect_reachable(nfa, NULL, NULL);
  }

  switch(nfa->value.type) {

    case NFA_SPLIT|NFA_PROGRESS: {
      thread_clone(sim, nfa->out2, -1);
      nfa = nfa->out1;
      SKIP_EPSILONS(nfa);
      while(nfa->value.type == NFA_EPSILON || nfa->value.literal == '?') {
        if(nfa->value.literal == '?') {
          nfa = nfa->out2;
          SKIP_EPSILONS(nfa);
        }
      }
      load_next(sim, nfa);
    } break;
    case NFA_SPLIT: {
      for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
        thread_clone(sim, list_get_at(&(nfa->reachable), i), -1);
      }
      load_next(sim, list_get_at(&(nfa->reachable), 0));
    } break;
    case NFA_INTERVAL: {
      int count = ++((sim->loop_record[nfa->id]).count);
      NFASim * clone = NULL;
      NFA *tmp = nfa;
      SKIP_EPSILONS(tmp);
      int reaches_accept = (nfa->out2->value.type == NFA_ACCEPTING) ? 1 : 0;
      tmp = NULL;
      if(nfa->value.min_rep > 0 && count < nfa->value.min_rep) {
        ++(sim->tracking_intervals);
        load_next(sim, list_get_at(&(nfa->reachable), 0));
      }
      else {
        if(nfa->value.max_rep > 0) {
          if(count < nfa->value.max_rep) {
            ++(sim->tracking_intervals);
            load_next(sim, list_get_at(&(nfa->reachable), 0));
            for(int i = 1; i < list_size(&(nfa->reachable)); ++i) {
              tmp = list_get_at(&(nfa->reachable), i);
              clone = thread_clone(sim, tmp, (reaches_accept) ? -1 : nfa->id);
            }
          }
          else {
            --(sim->tracking_intervals);
            (sim->loop_record[nfa->id]).count = 0;
            nfa = nfa->out2;
            SKIP_EPSILONS(nfa);
            load_next(sim, nfa);
          }
        }
        else {
          // unbounded upper limit
          // FIXME:  this is wrong
printf("{%d, %d} -- ", nfa->value.min_rep, nfa->value.max_rep);
printf("count: %d vs. max_rep: %d\n", count, nfa->value.max_rep);
          load_next(sim, nfa->out1);
        }
      }
    } break;
    case NFA_TREE: {
      NFASim * tmp_sim = sim;
      // deal the remaining branches to new threads
      for(int i = 1; i < list_size(nfa->value.branches); ++i) {
        thread_clone(tmp_sim, list_get_at(nfa->value.branches, i), -1);
      }
      load_next(sim, list_get_at(nfa->value.branches, 0));
    } break;
    case NFA_CAPTUREGRP_BEGIN: {
      sim->tracking_backrefs = 1;
      sim->backref_match[nfa->id].start = sim->input_ptr;
      sim->backref_match[nfa->id].end = NULL;
      load_next(sim, nfa->out2);
    } break;
    case NFA_CAPTUREGRP_END: {
//      ++(sim->sp);
//      --(sim->tracking_backrefs);
//      if(sim->sp >= MAX_BACKTRACK_DEPTH) {
//        fatal("Backtrack depth exceeds maximum\n");
//      }
//      push_backtrack_stack(sim, nfa);
      sim->backref_match[nfa->id].end = sim->input_ptr - 1;
      load_next(sim, nfa->out2);
    } break;
    default: {
      if(sim->tracking_intervals == 0
      && active_threads_sp[nfa->id] == sim->input_ptr) {
        sim->status = -1;
      }
      else {
        active_threads_sp[nfa->id] = sim->input_ptr;
        sim->ip = nfa;
        sim->status = (nfa->value.type == NFA_ACCEPTING) ? 1 : 0;
      }
    } break;
  }
  return sim->status;
}

/*
void
push_backtrack_stack(NFASim * sim, NFA * nfa)
{
  if((sim->match.end == NULL) && (CTRL_FLAGS(sim->scanner) & AT_BOL_FLAG)) {
    sim->backtrack_stack[sim->sp].input = get_cur_pos(sim->scanner);
  }
  else {
    sim->backtrack_stack[sim->sp].input = sim->input_ptr;
  }
  sim->backtrack_stack[sim->sp].restart_point = nfa;
  sim->backtrack_stack[sim->sp].match = sim->match.end;
}
*/

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
          load_next(sim, NEXT_INST(sim));
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_LITERAL: {
        if(INPUT(sim) == INST_TOKEN(sim)) {
          update_match(sim);
          ++(sim->input_ptr);
active_threads_sp[sim->ip->id] = NULL;
          load_next(sim, NEXT_INST(sim));
        }
        else {
          sim->status = -1;
        }
active_threads_sp[sim->ip->id] = NULL;
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
          load_next(sim, NEXT_INST(sim));
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
            load_next(sim, NEXT_INST(sim));
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
        #define BACKREF_START(sim) ((sim)->backref_match[(sim)->ip->id - 1].start)
        #define BACKREF_END(sim) ((sim)->backref_match[(sim)->ip->id - 1].end)
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
            load_next(sim, NEXT_INST(sim));
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
          load_next(sim, NEXT_INST(sim));
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_EOL_ANCHOR: {
        if(INPUT(sim) == EOL(sim)) {
active_threads_sp[sim->ip->id] = NULL;
          load_next(sim, NEXT_INST(sim));
        }
        else {
          sim->status = -1;
        }
      } break;
      case NFA_ACCEPTING: {
        sim->status = 1;
      } break;
    }
  }
  else {
    sim->status = 1;
  }

//  if(sim->sp > -1 && sim->status == -1){
//  && sim->backtrack_stack[sim->sp].count >= 0) {
//    pop_backtrack_stack(sim);
//  }

  return sim->status;
}


// NEED TO TRACK THREADS BY THE 'NFA-ID', 'INPUT-PTR' AND 'ACTIVE INTERVALS'
// IF ALL THREE ATTRIBUTES MATCH DON'T ADD THE NEW THREAD

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
  NFA * start_state = thread->ip;
  thread->input_ptr = input_pointer;
  load_next(thread, thread->ip);
  int match_found = 0;
  int current_run = 0;
  Match * m = NULL;
  while((*input_pointer) != '\0') {
    while(thread) {
      thread_step(thread);
      switch(thread->status) {
        case 1: {
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
        } // fall through
        case -1: {
          // kill thread
          thread = release_thread(thread, start_state);
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
