#include <stdlib.h>
#include <string.h>
#include "slist.h"
#include "token.h"
#include "misc.h"
#include "nfa.h"
#include "backtrack_recognizer.h"
#include "scanner.h"
#include <stddef.h>

#include <string.h>
#include <stdio.h>

#define INPUT(sim) (*((sim)->input_ptr))
#define EOL(sim)  ((sim)->scanner->eol_symbol)
#define NEXT_INST(sim) ((sim)->ip->out2)
#define CUR_INST(sim) ((sim)->ip)
#define RANGE(sim) (*((sim)->ip->value.range))
#define INST_TYPE(sim) ((sim)->ip->value.type)
#define INST_TOKEN(sim) ((sim)->ip->value.literal)
#define INST_LONG_TOKEN(sim) ((sim)->ip->value.lliteral[(sim)->ip->value.idx])
#define RELEASE_ALL_THREADS(t) while((t)) { (t) = release_thread((t)); }


void reset_nfa_sim(NFASim * sim, NFA * start_state);
void push_backtrack_stack(NFASim * sim, NFA * nfa);
int  pop_backtrack_stack(NFASim * sim);
int  load_next(NFASim *, NFA *);


void
update_longest_match(Match * m, Match * nm)
{
  if(m->end == 0 || ((m->end - m->start) < (nm->end - nm->start))) {
    *m = *nm;
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


__attribute__((always_inline)) static inline void
flush_matches(NFASimCtrl * ctrl)
{
  printf("%s", ctrl->matches);
  ctrl->match_idx = 0;
}


Match *
new_match(NFASimCtrl * ctrl)
{

  char * begin = ctrl->match.start;
  char * end = ctrl->match.end;
  Match * match = NULL;
  if(begin && (begin <= end)) {
    int sz = end - begin + 1;
    if((ctrl->match_idx + sz + 1) >= MATCH_BUCKET_SIZE) {
      flush_matches(ctrl);
    }
    strncpy((ctrl->matches) + ctrl->match_idx, begin, sz);
    ctrl->match_idx += sz;
    ctrl->matches[ctrl->match_idx] = '\n';
    ++(ctrl->match_idx);
    ctrl->matches[ctrl->match_idx] = '\0';
  }
  return match;
}


void *
print_and_release_match(void * match)
{
  if(match) {
    char * begin = ((Match*)match)->start;
    char * end = ((Match*)match)->end;
    char * ptr = begin;
    while(ptr <= end) {
      printf("%c", *ptr);
      ++ptr;
    }
    printf("\n");
  }
  return NULL;
}


static inline int
is_literal_in_range(nfa_range range, unsigned int c)
{
#define index(c)  ((c) / 32)
#define offset(c) ((c) % 32)
  unsigned int mask = 0x01 << (offset(c));
  int ret = 0;
  if(range[index(c)] & mask) {
    ret = 1;
  }
#undef index
#undef offset
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
    sim->sp = -1;
    load_next(sim, start_node);
  }
  else {
    fatal("Unable to obtain an execution threads\n");
  }
  return sim;
}


static inline NFASim *
thread_clone(NFASim * sim, NFA * nfa)
{
  NFASim * clone;
  if(list_size(sim->ctrl->thread_pool)) {
    clone = list_shift(sim->ctrl->thread_pool);
  }
  else {
    clone = malloc(sim->size);
  }

  clone->ip = sim->ip;
  clone->size = sim->size;
  clone->input_ptr = sim->input_ptr;
  clone->ctrl = sim->ctrl;
  clone->scanner = sim->scanner;
  clone->match = sim->match;
  clone->tracked_loop_count = sim->tracked_loop_count;

  memcpy(clone->backref_match, sim->backref_match, sizeof(Match) * MAX_BACKREF_COUNT);
  memcpy(clone->loop_record, sim->loop_record, sizeof(LoopRecord) * sim->tracked_loop_count);

  clone->next_thread = sim->next_thread;
  clone->prev_thread = sim;

  if(sim->prev_thread == sim) {
    sim->prev_thread = clone;
  }
  else {
    // close the loop
    sim->next_thread->prev_thread = clone;
  }

  sim->next_thread = clone;

  // don't backtrack past a branch point
  clone->sp = -1;

  load_next(clone, nfa);
  return clone;
}


static inline NFASim *
release_thread(NFASim * sim) 
{
  list_push(sim->ctrl->thread_pool, sim);
  sim->prev_thread->next_thread = sim->next_thread;
  sim->next_thread->prev_thread = sim->prev_thread;

  return (sim->next_thread == sim) ? NULL: sim->next_thread;
}


NFASim *
new_nfa_sim(Parser * parser, Scanner * scanner, ctrl_flags * cfl)
{
  NFASim * sim;
  int sz = sizeof(*sim) + (sizeof(*(sim->loop_record)) * (parser->loops_to_track));
  sim = xmalloc(sz);
  sim->size = sz;
  sim->ctrl = xmalloc(sizeof *(sim->ctrl));

  sim->ctrl->thread_pool = new_list();
  sim->ctrl->ctrl_flags  = cfl;
  sim->prev_thread = sim->next_thread = sim;

  if(parser->total_branch_count) {
    NFASim * thread = NULL;
    for(int i = 1; i < parser->total_branch_count; ++i) {
      thread = malloc(sim->size);
      list_push(sim->ctrl->thread_pool, thread);
    }
  }

  sim->tracked_loop_count = parser->loops_to_track;
  sim->scanner = scanner;
  sim->ip = peek(parser->symbol_stack);
  sim->sp = -1;
  return sim;
}


void
reset_nfa_sim(NFASim * sim, NFA * start_state)
{
  sim->sp = -1;
  sim->ip = start_state; 
  sim->match.start = sim->match.end = NULL;
  sim->next_thread = sim->prev_thread = sim;
  memset(sim->loop_record,   0, sizeof(LoopRecord) * sim->tracked_loop_count);
}


int
load_next(NFASim * sim, NFA * nfa)
{
#define NFA_TREE_BRANCH(s, idx) list_get_at((*(NFA **)(s))->value.branches, (idx))
#define BT_RECORD(s)            ((s)->backtrack_stack)[(s)->sp]

  int found_accepting = 0;
  switch(nfa->value.type) {
    case NFA_ACCEPTING: {
      found_accepting = 1;
    } break;
    case NFA_SPLIT|NFA_PROGRESS: {
      sim->loop_record[nfa->id].last_match = get_cur_pos(sim->scanner);
      ++sim->loop_record[nfa->id].count;
    } // fallthrough
    case NFA_SPLIT: {
      ++(sim->sp);
      if(sim->sp >= MAX_BACKTRACK_DEPTH) {
        fatal("Backtrack depth exceeds maximum\n");
      }
      push_backtrack_stack(sim, nfa);
      switch(nfa->value.literal) {
        case '+':
        case '*': {
          nfa = (nfa->greedy) ? nfa->out1 : nfa->out2;
        } break;
        case '?': {
            nfa = (nfa->greedy) ? nfa->out2 : nfa->out1;
        } break;
        default: {
          // should never hit this condition
          fatal("Expected ?, * or +\n");
        }
      }
      found_accepting += load_next(sim, nfa);
    } break;
    case NFA_INTERVAL: {
      if(sim->sp >= MAX_BACKTRACK_DEPTH) {
        fatal("Backtrack depth exceeds maximum\n");
      }
      unsigned int count = ++((sim->loop_record[nfa->id]).count);
      if(nfa->value.max_rep > 0) {
        if(count < nfa->value.max_rep) {
          ++(sim->sp);
          push_backtrack_stack(sim, nfa);
          sim->backtrack_stack[sim->sp].count = count; 
          nfa = nfa->out1;
        }
        else {
          ++(sim->sp);
          push_backtrack_stack(sim, nfa);
          sim->backtrack_stack[sim->sp].count = count; 
          sim->loop_record[nfa->id].count = 0;
          nfa = nfa->out2;
        }
      }
      else {
        // unbounded upper limit
        ++(sim->sp);
        push_backtrack_stack(sim, nfa);
        sim->backtrack_stack[sim->sp].count = count; 
        nfa = nfa->out1;
      }
      found_accepting += load_next(sim, nfa);
    } break;
    case NFA_TREE: {
      // grab the first branch for the current thread
      sim->ip = list_get_at(nfa->value.branches, 0);
      NFASim * tmp_sim = sim;
      // deal the remaining branches to new threads
      for(int i = 1; i < list_size(nfa->value.branches); ++i) {
        tmp_sim = thread_clone(tmp_sim, list_get_at(nfa->value.branches, i));
      }
      found_accepting += load_next(sim, sim->ip);
    } break;
    case NFA_EPSILON: {
      found_accepting += load_next(sim, nfa->out2);
    } break;
    case NFA_CAPTUREGRP_BEGIN: {
      sim->backref_match[nfa->id].start = sim->input_ptr;
      sim->backref_match[nfa->id].end = NULL;
      found_accepting += load_next(sim, nfa->out2);
    } break;
    case NFA_CAPTUREGRP_END: {
      ++(sim->sp);
      if(sim->sp >= MAX_BACKTRACK_DEPTH) {
        fatal("Backtrack depth exceeds maximum\n");
      }
      push_backtrack_stack(sim, nfa);
      sim->backref_match[nfa->id].end = sim->input_ptr - 1;
      found_accepting += load_next(sim, nfa->out2);
    } break;
    default: {
      sim->ip = nfa;
      found_accepting = 0;
    } break;
  }
#undef NFA_TREE_BRANCH
#undef BT_RECORD
  return found_accepting;
}


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


int
pop_backtrack_stack(NFASim * sim)
{
#define LOOP_INST(sim) ((sim)->ip->out1)
#define BACKTRACK_STACK(sim, i) ((sim)->backtrack_stack[(i)])
#define STACK_INST(sim) (BACKTRACK_STACK((sim), (sim)->sp).restart_point)
#define STACK_INST_AT(sim, i) (BACKTRACK_STACK((sim), (i)).restart_point)

  int accept = 0;
  if(sim->sp > -1) {
    BacktrackRecord btrec = sim->backtrack_stack[sim->sp];
    NFA * bt_inst = btrec.restart_point;
    sim->input_ptr = btrec.input;
    sim->match.end = btrec.match;
    switch(bt_inst->value.type) {
      case NFA_SPLIT|NFA_PROGRESS:
      case NFA_SPLIT: {
        NFA * next_inst = NULL;
        switch(bt_inst->value.literal) {
          case '?': {
            if(bt_inst->greedy) {
              next_inst = bt_inst->out1;
              while(next_inst->value.type == NFA_EPSILON) {
                next_inst = next_inst->out2;
              }
              if((next_inst->value.type == (NFA_SPLIT|NFA_PROGRESS))
              && (sim->loop_record[next_inst->id].last_match == sim->input_ptr)) {
                next_inst = next_inst->out2;
              }
            }
            else {
              next_inst = bt_inst->out2;
            }
            int cur_sp = sim->sp;
            --(sim->sp);
            accept = load_next(sim, next_inst);
            while(STACK_INST(sim) == bt_inst) {
              sim->sp = cur_sp - 1;
              accept = load_next(sim, STACK_INST_AT(sim, cur_sp)->out2);
            }
          } break;
          case '*': // fallthrough
          case '+': {
            next_inst = (bt_inst->greedy) ? bt_inst->out2 : bt_inst->out1;
            while(next_inst->value.type == NFA_EPSILON) {
              next_inst = next_inst->out2;
            }
            if((next_inst->value.type == (NFA_SPLIT|NFA_PROGRESS))
            && (sim->loop_record[next_inst->id].last_match == sim->input_ptr)) {
              next_inst = next_inst->out2;
            }
            --(sim->sp);
            accept = load_next(sim, next_inst);
          } break;
        }
      } break;
      case NFA_CAPTUREGRP_END: {
        sim->backref_match[CUR_INST(sim)->id - 1].end = NULL;
        --(sim->sp);
        accept = pop_backtrack_stack(sim);
      } break;
      case NFA_INTERVAL: {
        // give up the match and let the next token try to match it
        sim->loop_record[bt_inst->id].count = 0;
        --(sim->sp);
        if(bt_inst->value.max_rep > 0 && btrec.count == bt_inst->value.max_rep) {
          accept = pop_backtrack_stack(sim);
        }
        else if(btrec.count >= bt_inst->value.min_rep) {
          accept = load_next(sim, bt_inst->out2);
        }
        else {
          accept = pop_backtrack_stack(sim);
        }
        return accept;
      } break;
    }
    return accept;
  }
  else {
    return -1;
  }
#undef LOOP_INST
#undef STACK_INST
#undef STACK_INST_AT
#undef BACKTRACK_STACK
}


static inline int
thread_step(NFASim * sim)
{
  if(INPUT(sim) == '\0')
    return -1;

  int accept = 0;
  switch(INST_TYPE(sim)) {
    case NFA_ANY: {
      if(INPUT(sim) != EOL(sim)) {
        update_match(sim);
        ++(sim->input_ptr);
        accept = load_next(sim, NEXT_INST(sim));
      }
      else {
        accept = pop_backtrack_stack(sim);
      }
    } break;
    case NFA_LITERAL: {
      if(INPUT(sim) == INST_TOKEN(sim)) {
        update_match(sim);
        ++(sim->input_ptr);
        accept = load_next(sim, NEXT_INST(sim));
      }
      else {
        accept = pop_backtrack_stack(sim);
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
        accept = load_next(sim, NEXT_INST(sim));
      }
      else {
        sim->ip->value.idx = 0;
        accept = pop_backtrack_stack(sim);
      }
    } break;
    case NFA_RANGE: {
      if(INPUT(sim) != EOL(sim)) {
        if(is_literal_in_range(RANGE(sim), INPUT(sim))) {
          update_match(sim);
          ++(sim->input_ptr);
          accept = load_next(sim, NEXT_INST(sim));
        }
        else {
          accept = pop_backtrack_stack(sim);
        }
        break;
      }
      accept = pop_backtrack_stack(sim);
    } break;
    case NFA_BACKREFERENCE: {
      // FIXME: id - 1 should just be id
      #define BACKREF_START(sim) ((sim)->backref_match[(sim)->ip->id - 1].start)
      #define BACKREF_END(sim) ((sim)->backref_match[(sim)->ip->id - 1].end)
      char * bref_end = BACKREF_END(sim);
      if(bref_end) {
        int fail = 0;
        char * tmp_input = sim->input_ptr;
        char * bref_ptr = BACKREF_START(sim);
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
          accept = pop_backtrack_stack(sim);
        }
        else {
          sim->input_ptr = tmp_input - 1;
          update_match(sim);
          ++(sim->input_ptr);
          accept = load_next(sim, NEXT_INST(sim));
        }
      }
      else {
        accept = pop_backtrack_stack(sim);
      }
      #undef BACKREF_START
      #undef BACKREF_END
    } break;
    case NFA_BOL_ANCHOR: {
      if(sim->input_ptr == sim->scanner->buffer) {
        accept = load_next(sim, NEXT_INST(sim));
      }
      else {
        accept = pop_backtrack_stack(sim);
      }
    } break;
    case NFA_EOL_ANCHOR: {
      if(INPUT(sim) == EOL(sim)) {
        accept = load_next(sim, NEXT_INST(sim));
      }
      else {
        accept = pop_backtrack_stack(sim);
      }
    } break;
  }

  return accept;
}


int
update_input_pointer(char ** input_pointer, NFASim * thread)
{
  if((*input_pointer) < thread->match.end) {
    ++(*input_pointer);
    return 0;
  }
  return 1;
}


int
run_nfa(NFASim * thread)
{
  NFASimCtrl * ctrl = thread->ctrl;
  ctrl->match.start = NULL;
  ctrl->match.end = NULL;
  char * input_pointer = get_cur_pos(thread->scanner);
  NFA * start_state = thread->ip;
  thread->input_ptr = input_pointer;
  load_next(thread, thread->ip);
  int match_found = 0;
  Match * m = NULL;
  if(INST_TYPE(thread) != NFA_ACCEPTING) {
    while((*input_pointer) != '\0') {
      while(thread) {
        int res = thread_step(thread);
        switch(res) {
          case 1: {
            match_found = 1;
            update_longest_match(&(ctrl->match), &(thread->match));
            if(((CTRL_FLAGS(ctrl) & MGLOBAL_FLAG) == 0)) {
              new_match(ctrl);
              goto RELEASE_ALL_THREADS;
            }
            update_input_pointer(&input_pointer, thread);
          } // fall through
          case -1: {
            // kill thread
            thread = release_thread(thread);
          } break;
          case 0: {
            // keep trying
            thread = thread->next_thread;
          } break;
        }
      }
      new_match(ctrl);
      input_pointer = (ctrl->match.end) ? ctrl->match.end + 1 : input_pointer + 1;
      ctrl->match.start = NULL;
      ctrl->match.end = NULL;
      thread = reset_thread(ctrl, start_state, input_pointer);
      thread->input_ptr = input_pointer;
    }
  }
RELEASE_ALL_THREADS:
  RELEASE_ALL_THREADS(thread);

  return match_found;
}


void
free_nfa_sim(NFASim * nfa_sim)
{
  list_iterate(((*(NFASimCtrl **)nfa_sim)->thread_pool), (void *)&free);
  list_free(&((*(NFASimCtrl **)nfa_sim)->thread_pool), NULL);

  NFASim * next_thread = nfa_sim;

  while(nfa_sim->next_thread != nfa_sim && (next_thread = nfa_sim->next_thread)) {
    free(nfa_sim);
  }

  free(nfa_sim->ctrl);
  free(nfa_sim);
}


// Move this out of this file once done testing
int
main(int argc, char ** argv)
{
  ctrl_flags cfl;
  Scanner * scanner = NULL;
  Parser  * parser  = NULL;
  NFASim  * nfa_sim = NULL;
  FILE    * file    = NULL;

  char * buffer = NULL;
  size_t buf_len = 0;
  unsigned int line_len = 0;

  if(argc >= 2) {
    file = fopen(argv[1], "r");
    if(file == NULL) {
      fatal("UNABLE OPEN REGEX FILE\n");
    }
    line_len = getline(&buffer, &buf_len, file);
    scanner  = init_scanner(buffer, buf_len, line_len, &cfl);

    if(scanner->line_len < 0) {
      fatal("UNABLE READ REGEX FILE\n");
    }
    parser = init_parser(scanner, &cfl);
  }
  else {
    fatal("NO INPUT FILE PROVIDED\n");
  }

//printf("--> PHASE1: PARSING/COMPILING regex\n\n");
//printf("EOL_FAG %s\n", (scanner->ctrl_flags & EOL_FLAG) ? "SET" : "NOT SET");

  int run = parse_regex(parser);
  fclose(file);

  if(run &&  (argc > 2)) {
    FILE * search_input = fopen(argv[2], "r");
    if(search_input == NULL) {
      fatal("Unable to open file\n");
    }
//printf("\n--> RUNNING NFA SIMULAITON\n\n");

    int line = 0;
    nfa_sim = new_nfa_sim(parser, scanner, &cfl);
    /*
     * Create and initialize nfa_sim object.
     * Load the epsilon closure for the start state of the nfa;
     * freeze the initial states in sim->ctrl->freeze_states... this will allow
     * faster sim resets. Returns a handle to the static sim->ctrl.
     *
     * NFASimCtrl * thread_ctrl = init_nfa_sim(parser, scanner, &cfl);
     *
     * This call should replace the above call to new_nfa_sim and the following
     * assignment of thread_ctrl.
     */
    NFASimCtrl * thread_ctrl = nfa_sim->ctrl;
    SET_MGLOBAL_FLAG(&CTRL_FLAGS(scanner));
    while((scanner->line_len = getline(&scanner->buffer, &scanner->buf_len, search_input)) > 0) {
      reset_scanner(scanner);
      reset_nfa_sim(nfa_sim, ((NFA *)peek(parser->symbol_stack))->parent);
      run_nfa(nfa_sim);
      /*
       * Load the states from the (*(NFASimCtrl **)sim)->freeze_states
       * return nfa_sim loaded with all initial links to nfas that form part
       * of the epsilon closure of the nfa's start state.
       *
       * nfa_sim = reset_nfa_sim(thread_ctrl);
       *
       * The above call should replace the following two calls.
       */
      nfa_sim = list_shift(thread_ctrl->thread_pool);
      nfa_sim->prev_thread = nfa_sim->next_thread = nfa_sim;
    }

    if(thread_ctrl->match_idx) {
      printf("%s", thread_ctrl->matches);
    }

    fclose(search_input);
  }

  parser_free(parser);
  free_scanner(scanner);

  if(nfa_sim) {
    free_nfa_sim(nfa_sim);
  }

  return 0;
}
