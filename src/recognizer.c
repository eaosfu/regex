#include <stdlib.h>
#include "slist.h"
#include "token.h"
#include "misc.h"
#include "nfa.h"
#include "recognizer.h"
#include "scanner.h"

#include <string.h>
#include <stdio.h>

// TESTING
void
print_backref_match(NFASim * sim, int id)
{
  char * cgrp_match = sim->backref_matches[id].start;
  char * end = sim->backref_matches[id].end;
printf("\tBACKREF MATCHES: '");
  while(cgrp_match <= end) {
    printf("%c", cgrp_match[0]);
    ++cgrp_match;
  }
printf("'\n");

}
// END TESTING



void
clear_stateset2_ids(NFASim * sim)
{
  memset(sim->stateset2_ids, 0, WORKING_SET_ID_BITS);
  return;
}


int
id_in_stateset2(NFASim * sim, unsigned int id)
{
#define index(id)  ((id) / UINT_BITS)
#define offset(id) (((id) % UINT_BITS) - 1)
  return ((sim->stateset2_ids)[index(id)] & (0x01 << offset(id)));
#undef index
#undef offset
}


void
add_id_to_stateset2(NFASim * sim, unsigned int id)
{
#define index(id)  ((id) / UINT_BITS)
#define offset(id) (((id) % UINT_BITS) - 1)
  //(sim->stateset2_ids)[index(id)] |= (0x01 << ((id) ? offset(id) : 0));
  (sim->stateset2_ids)[index(id)] |= (0x01 << offset(id));
#undef index
#undef offset
  return;
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
//    printf("FREEING MATCH OBJECT\n");
    free(m);
  }
  return ((void *)NULL);
}


Match *
record_match(char * buffer, List * matches, char * start, char * end, int new_match)
{
#define MATCH_BUFFER(m) ((char *)((Match *)(m)->head->data)->buffer)
  Match * match = NULL;
  //if(matches->head != NULL && buffer == MATCH_BUFFER(matches) && !new_match) {
  if(new_match == 0) {
    // Extend a previous match
    match = matches->head->data;
    if((start - match->end) <= 1) {
      match->end = end;
      goto RETURN;
    }
  }
  match = xmalloc(sizeof * match);
  match->buffer = buffer;
  match->start  = start;
  match->end    = end;

  list_push(matches, match);
#undef MATCH_BUFFER
RETURN:
  return match;
}


// We want to grab the next position in the buffer if the '(' occurs after we've matched
// some input. Otherwise we need to get the current position in the buffer; the in_match
// flag let's us know which case we're dealing with.
void
record_capturegroup_match(NFASim * sim, unsigned int capturegroup_idx, unsigned int flag, int in_match)
{
  if(flag == NFA_CAPTUREGRP_BEGIN) {
    sim->backref_matches[capturegroup_idx].buffer = sim->scanner->buffer;
    if(in_match == 0 ) {
      sim->backref_matches[capturegroup_idx].start = get_cur_pos(sim->scanner);
    }
    else {
      sim->backref_matches[capturegroup_idx].start = get_scanner_readhead(sim->scanner);
    }
    // If the sim reset count is greater than the count recorded by the match then
    // this is the first time processing this capture group. Otherwise we're in a loop
    if(sim->reset_count > sim->backref_matches[capturegroup_idx].last_reset) {
      // set end to NULL so when we process a backreference we can tell whether or
      // not the associated capture group matched.
      sim->backref_matches[capturegroup_idx].end = NULL;
      sim->backref_matches[capturegroup_idx].count = 0;
    }
  }
  else {
    // if we reach this point all that remains is to mark where the match ends.
    sim->backref_matches[capturegroup_idx].end = get_cur_pos(sim->scanner);
    ++(sim->backref_matches[capturegroup_idx].count);
  }

  return;
}


NFASim *
new_nfa_sim(Parser * parser, Scanner * scanner, ctrl_flags * cfl)
{
  NFASim * sim = xmalloc(sizeof * sim);
  sim->ctrl_flags = cfl;
  sim->scanner = scanner;
  sim->parser = parser;
  sim->nfa = peek(parser->symbol_stack);
  sim->state_set1 = new_list();
  sim->state_set2 = new_list();
//  sim->mmrecord_stack = new_stack();
  sim->matches = new_list();
  return sim;
}


void
reset_nfa_sim(NFASim * sim)
{
  ++sim->reset_count;
  list_clear(sim->state_set1);
  list_clear(sim->state_set2);
  clear_stateset2_ids(sim);
  get_states(sim, sim->nfa->parent, sim->state_set1, 0, 1, 0);
  clear_stateset2_ids(sim);
}


// int
// backtrack(NFASim * sim)
// {
//   MUltiMatchRecord * mmecord = peek(sim->mmrecord_stack);
//   while(mmrecord && (mmrecord->min_sz >= mmrecord->count)) {
//     pop(sim->mmrecord_stack);
//     if(mmrecord->cgrp) {
//       if(mmrecord->sibling) {
//         mmercord = peek(sim->mmrecord_stack);
//         break;
//       }
//     }
//     mmercord = peek(sim->mmrecord_stack);
//   }
//
//   if(mmrecord) {
//     --(mmrecord->count);
//     // the mmrecord we hold now may or may not be part of a cgrp.
//     // consider the case: a+(b+)x\1
//     // b+ is a sibling of a+ which itself is not part of a cgrp.
//     if(mmrecord->cgrp) {
//       --(mmrecord->cgrp->end);
//     }
//     list_append(sim->state_set2, mmrecord->start);
//     return 1;
//   }
//
//   return 0;
// }
//
//
// MUltiMatchRecord * 
// new_mmrecord(NFA * start, NFA * end, Match * cgrp) {
//   MUltiMatchRecord * mmrecord = malloc(sizeof(*mmrecord));
//   mmrecord->start = start;
//   mmrecord->end = end;
//   mmrecord->count = 1;
//   mmrecord->min_sz = end->value.last_match - start->value.last_match;
// }
//
//
// void
// track_mmrecord(sim, NFA * start, NFA *end, int cgrp)
// {
//   MUltiMatchRecord * mmecord = peek(sim->mmrecord_stack);
//   if(mmrecord && (mmrecord->start == start) && (mmrecord->end == end)) {
//     // this is an extension of our last mmrecord
//     ++(mmercord->count);
//     if(mmrecord->cgrp) {
//       ++(mmrecord->cgrp->end);
//     }
//   }
//   else {
//     MUltiMatchRecord * sibling = deep_peek(sim->mmrecord_stack, 2);
//     int distance = (mmrecord->start->match_pos - sibling->end->match_pos);
//     if(sibling) {
//       switch(distance) {
//         case 0: {
//           // squabbling siblings... favor the youngest
//         } break;
//         case 1: {
//           // perefect siblings
//         } break;
//         default:
//       }
//     mmrecord = new_mmrecord(start, end, (sim->backref_matches)[cgrp - 1]);
//     }
//     push(sim->mmrecord_stack, mmrecord);
//   }
// }


int
get_states(NFASim * sim, NFA * nfa, List * lp, int in_match, int first_entry, unsigned int origin)
{
#define IS_NFA_TREE(nfa)                         \
  (nfa->value.type & NFA_TREE)

#define IS_NFA_ACCEPT(nfa)                       \
  (nfa->value.type & NFA_ACCEPTING)

#define IS_NFA_SPLIT(nfa)                        \
  ((nfa)->value.type & NFA_SPLIT)

#define IS_BACKREFERENCE(nfa)                    \
  ((nfa)->value.type & NFA_BACKREFERENCE)

#define IS_NFA_INTERVAL(nfa)                    \
  ((nfa)->value.type & NFA_INTERVAL)

#define MARK_STATES(sim)                         \
  (CTRL_FLAGS(sim) & MARK_STATES_FLAG)

#define IS_MATCHABLE_OR_INTERVAL_NFA(nfa)        \
  ((nfa)->value.type & ~(NFA_SPLIT             | \
                         NFA_EPSILON           | \
                         NFA_TREE              | \
                         NFA_CAPTUREGRP_BEGIN  | \
                         NFA_CAPTUREGRP_END))

#define IS_CAPTUREGRP_MARKER(nfa)                \
  ((nfa)->value.type & (NFA_CAPTUREGRP_BEGIN |   \
                        NFA_CAPTUREGRP_END))

  int found_accepting_state = 0;

  if(IS_MATCHABLE_OR_INTERVAL_NFA(nfa)) {
    if(first_entry || (id_in_stateset2(sim, nfa->id) == 0)) {
      if((found_accepting_state = IS_NFA_ACCEPT(nfa)) != NFA_ACCEPTING) {
        add_id_to_stateset2(sim, nfa->id);
        list_append(lp, nfa);
        if(IS_NFA_INTERVAL(nfa) && origin == 0) {
          nfa->value.count = 0;
        }
      }
    }
  }
  else {
    if(IS_NFA_TREE(nfa)) {
      ListItem * li = nfa->value.branches->head;
      unsigned int list_sz = list_size(nfa->value.branches);
      for(int i = 0; i < list_sz; ++i) {
        nfa = li->data;
        found_accepting_state += get_states(sim, nfa, lp, in_match, 0, origin);
        li = li->next;
      }
    }
    else {
      if(IS_NFA_SPLIT(nfa)) {
        // Avoid endlessly adding the same nodes in epsilon
        // closure containing loops such as in cases like:
        // (<expression>?)+
        int list_sz = list_size(lp);
        int accepting = found_accepting_state;
        found_accepting_state += get_states(sim, nfa->out2, lp, in_match, 0, origin);
        if(first_entry || list_size(lp) != list_sz || accepting != found_accepting_state) {
//printf("\tLOOPING: nfa id --  %d\n", nfa->id);
          found_accepting_state += get_states(sim, nfa->out1, lp, in_match, 0, nfa->id);
        }
      }
      else if(IS_CAPTUREGRP_MARKER(nfa)) {
        record_capturegroup_match(sim, nfa->id, nfa->value.type, in_match);
        found_accepting_state += get_states(sim, nfa->out2, lp, in_match, 0, origin);
      }
      else{
        found_accepting_state += get_states(sim, nfa->out2, lp, in_match, 0, origin);
      }
    }
  }
#undef IS_NFA_SPLIT
#undef IS_NFA_ACCEPT
#undef NOT_SEEN_BEFORE
#undef IS_BACKREFERNECE
#undef IS_CAPTUREGRP_MAKER
#undef IS_MATCHABLE_OR_INTERVAL_NFA
  return found_accepting_state;
}


int
run_nfa(NFASim * sim)
{
//#define NEXT_STATE(n) ((NFA *)(n)->data)->out2
#define NEXT_STATE(n) ((n)->out2)
#define STATE_HEAD(n) ((NFA *)(n)->data)->out1
#define BACKREF_MATCH_LEN(sim, idx)                                            \
  (((sim)->backref_matches)[(idx)].end - ((sim)->backref_matches)[(idx)].start)

  ListItem * current_state;
  int match_start = 0;
  int match_end   = 0;
  int c = next_char(sim->scanner);
  int accept = 0;
  int eol_adjust = 0;
  int new_match = 1;
  int unrecorded_match;
  NFA * nfa;
  
  reset_nfa_sim(sim);
int t = 0;
  while(c != '\0') {
++t;
    current_state = sim->state_set1->head;
    unrecorded_match = 1;
    for(int i = 0; i < list_size(sim->state_set1); ++i) {
      nfa = current_state->data;
      accept = 0;
      eol_adjust = 0;
      switch(nfa->value.type) {
        case NFA_INTERVAL: {
          ++nfa->value.count;
//printf("INTERVAL [0x%x]: count: %d\n", nfa, nfa->value.count);
          if(nfa->value.count < nfa->value.min_rep) {
            get_states(sim, STATE_HEAD(current_state), sim->state_set1, 1, 1, nfa->id);
          }
          else if(nfa->value.count < nfa->value.max_rep) {
            get_states(sim, STATE_HEAD(current_state), sim->state_set1, 1, 1, nfa->id);
            if(nfa->parent == 0) {
              // no need to backtrack;
//printf("\t NO PARENT: %d\n", nfa->value.count);              
              accept = get_states(sim, NEXT_STATE(nfa), sim->state_set1, 1, 1, nfa->id);
            }
          }
          else {
            if(nfa->parent) {
               NFA * parent_interval = nfa->parent;
            }
//printf("LOAD NEXT INTERVAL AFTER: %d matches\n", nfa->value.count);
            accept = get_states(sim, NEXT_STATE(nfa), sim->state_set1, 1, 1, nfa->id);
            nfa->value.count = 0;
          }
//printf("accept: %d\n", accept);
        } break;
        case NFA_BACKREFERENCE: {
          // check if capture group has matched anything
          if(((sim->backref_matches)[nfa->id - 1].end) == 0 
          || c == sim->scanner->eol_symbol) {
            break;
          }

          int br_match_status = 0;
          char * cgrp_match = (sim->backref_matches)[nfa->id - 1].start;
          char * cgrp_match_end = (sim->backref_matches)[nfa->id - 1].end;
          char * tmp_input = get_cur_pos(sim->scanner);
          int br_idx = 0;

          while(cgrp_match <= cgrp_match_end) {
            if(tmp_input[0] != cgrp_match[0]
            || tmp_input[0] == sim->scanner->eol_symbol) {
              br_match_status = 0;
              break;
            }
            ++br_idx;
            ++cgrp_match;
            ++tmp_input;
            br_match_status = 1;
          }

          if(br_match_status > 0) {
            match_end += br_idx - 1;
            // make sure readhead is properly updated to ensure proper
            // recording of capture-groups
            // FIXME: replace this call with restart_from
            sim->scanner->readhead = tmp_input;
            accept = get_states(sim, NEXT_STATE(nfa), sim->state_set2, 1, 1, nfa->id);
          }
        } break;
        case NFA_ANY: {
          if(c != sim->scanner->eol_symbol) {
            accept = get_states(sim, NEXT_STATE(nfa), sim->state_set2, 1, 1, nfa->id);
//            nfa->value.last_match = get_cur_pos(sim->scanner);
          }
        } break;
        case NFA_RANGE: {
          if(c != sim->scanner->eol_symbol) {
            if(is_literal_in_range(*(nfa->value.range), c)) {
              accept = get_states(sim, NEXT_STATE(nfa), sim->state_set2, 1, 1, nfa->id);
//              nfa->value.last_match = get_cur_pos(sim->scanner);
            }
          }
        } break;
        case NFA_BOL_ANCHOR: {
          if(CTRL_FLAGS(sim) & AT_BOL_FLAG) {
            accept = get_states(sim, NEXT_STATE(nfa), sim->state_set1, 1, 1, nfa->id);
//            nfa->value.last_match = get_cur_pos(sim->scanner);
          }
        } break;
        case NFA_EOL_ANCHOR: {
          if(c == sim->scanner->eol_symbol) {
            eol_adjust = 1;
            accept = 1;
//            nfa->value.last_match = get_cur_pos(sim->scanner);
          }
        } break;
        case NFA_LONG_LITERAL: {
          if(c == nfa->value.lliteral[nfa->value.idx]) {
            nfa->value.idx = (++nfa->value.idx) % nfa->value.len;
            if(nfa->value.idx == 0) {
              accept = get_states(sim, NEXT_STATE(nfa), sim->state_set2, 1, 1, nfa->id);
            }
            else {
              // append state as-is
              accept = get_states(sim, nfa, sim->state_set2, 1, 1, nfa->id);
              //list_append(sim->state_set2, nfa);
            }
//            nfa->value.last_match = get_cur_pos(sim->scanner);
          }
          else {
            nfa->value.idx = 0;
          }
        } break;
        case NFA_LITERAL|NFA_IN_INTERVAL: {
          if(c == nfa->value.literal) {
//printf("%d: matched: %c\n", t, c);
            accept = get_states(sim, NEXT_STATE(nfa), sim->state_set2, 1, 1, nfa->id);
          }
          else {
            #define LINTERVAL(nfa)         ((nfa)->parent)
            #define LINTERVAL_COUNT(nfa)   ((nfa)->parent->value.count)
            #define LINTERVAL_MIN_REP(nfa) ((nfa)->parent->value.min_rep)
            if(LINTERVAL_COUNT(nfa) == LINTERVAL_MIN_REP(nfa)) {
//printf("%d: NEED TO BACKTRACK? %c vs. %c\n", t, c, nfa->value.literal);
              accept = get_states(sim, NEXT_STATE(LINTERVAL(nfa)), sim->state_set1, 1, 1, nfa->id);
            }
            else if(LINTERVAL_COUNT(nfa) >= LINTERVAL_MIN_REP(nfa)) {
              // if we were the only state able to match this far
              // retract the input to the where the interval began matching
              if(list_size(sim->state_set1) == 1) {
//printf("%d-- NEED TO BACKTRACK AFTER %d MATCHES?\n", t, LINTERVAL_COUNT(nfa));
              }
              else {
                accept = get_states(sim, NEXT_STATE(LINTERVAL(nfa)), sim->state_set1, 1, 1, nfa->id);
              }
            }
            LINTERVAL_COUNT(nfa) = 0;
          }
//printf("-- accept: %d\n", accept);
          #undef LINTERVAL
          #undef LITERAL_INTERVAL_COUNT
          #undef LITERAL_INTERVAL_MIN_REP
        } break;
        default: {
          if(c == nfa->value.literal) {
            accept = get_states(sim, NEXT_STATE(nfa), sim->state_set2, 1, 1, nfa->id);
//            nfa->value.last_match = get_cur_pos(sim->scanner);
          }
        } break;
      }

      if(unrecorded_match && accept) {
        record_match(sim->scanner->buffer,
                     sim->matches, 
                     sim->scanner->str_begin,
                     get_cur_pos(sim->scanner) - eol_adjust,
                     new_match);
        match_start = match_end;
        new_match = 0;
        unrecorded_match = 0;
      }

      // load the next item in state_set1
      current_state = current_state->next;
    }

#define GLOBAL_MATCH(s) (CTRL_FLAGS(s) & MGLOBAL_FLAG)
#define MATCH_COUNT(s)  ((s)->matches->size)
#define MATCH_FAILED(s) (((s)->state_set2->size == 0) ? 1 : 0)
#define CONTINUE_MATCHING(s)                            \
  ((GLOBAL_MATCH(s)) ? 1 : (MATCH_COUNT(s) == 1) ? 0 : 1)

    if(MATCH_FAILED(sim)) {
// IF WE CAN BACKTRACK... DO IT HERE
//    if(backtrack(sim) == 0) {
        if(CONTINUE_MATCHING(sim) == 0) {
          break;
        }
        ++match_start;
        restart_from(sim->scanner, (sim->scanner->buffer + match_start));
        c = next_char(sim->scanner);
        match_end = match_start;
        new_match = 1;
        // need to call reset_nfa_sim inorder to ensure proper recording
        // of capture-groups.
        reset_nfa_sim(sim);
//    }
    }
    else {
      list_swap(sim->state_set1, sim->state_set2);
      list_clear(sim->state_set2);
      clear_stateset2_ids(sim);
      match_end += 1;
      c = next_char(sim->scanner);
    }
  }
#undef NEXT_STATE
#undef GLOBAL_MATCH
#undef CONTINUE_MATCHING
  return SUCCESS;
}


void
print_matches(List * match_list)
{
  ListItem * m = match_list->head = list_reverse(match_list->head);
  for(int i = 0; i < match_list->size; ++i, m = m->next) {
    char * start = ((Match *)m->data)->start; 
    char * end = ((Match *)m->data)->end;
    int match_len = end - start + 1;
    for(int j = 0; j < match_len; j++) {
printf("%c", start[j]); 
    }
printf("\n");
free_match_string(m->data);
//printf("SHOWING NEXT MATCH\n");
  }
}


void
free_nfa_sim(NFASim* nfa_sim)
{
  list_free(&(nfa_sim->state_set1), NULL);
  list_free(&(nfa_sim->state_set2), NULL);
  //list_free(nfa_sim->matches, free_match_string);
  list_free(&(nfa_sim->matches), NULL);
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
    /*printf("Parsing file: %s\n", argv[1])*/
    file = fopen(argv[1], "r");
    if(file == NULL) {
      fatal("UNABLE OPEN REGEX FILE\n");
    }
    line_len = getline(&buffer, &buf_len, file);
    scanner = init_scanner(buffer, buf_len, line_len, &cfl);
/*printf("REGEX: '%s'\n", scanner->buffer)*/

//check_match_anchors(scanner);
    if(scanner->line_len < 0) {
      fatal("UNABLE READ REGEX FILE\n");
    }
    parser = init_parser(scanner, &cfl);
  }
  else {
    fatal("NO INPUT FILE PROVIDED\n");
  }

  /*printf("--> PHASE1: PARSING/COMPILING regex\n\n")*/
//printf("EOL_FAG %s\n", (scanner->ctrl_flags & EOL_FLAG) ? "SET" : "NOT SET");
  parse_regex(parser);
  fclose(file);

  if(argc > 2) {
    FILE * search_input = fopen(argv[2], "r");

    if(search_input == NULL) {
      fatal("Unable to open file\n");
    }
    int line = 0;

    /*printf("\n--> RUNNING NFA SIMULAITON\n\n")*/
    nfa_sim = new_nfa_sim(parser, scanner, &cfl);
SET_MGLOBAL_FLAG(&CTRL_FLAGS(scanner));
//printf("MGLOBAL MATCH IS %s SET\n", (scanner->ctrl_flags & MGLOBAL_FLAG) ? "": "NOT");
    while((scanner->line_len = getline(&scanner->buffer, &scanner->buf_len, search_input)) > 0) {
      ++line;
      scanner->line_no = line;
      reset_scanner(scanner);
//printf("NEW BUFFER:%d %d, '%s'\n", line, scanner->line_len, nfa_sim->scanner->buffer);
//#include<unistd.h>
//sleep(1);
      run_nfa(nfa_sim);
      if(nfa_sim->matches->size) {
//printf("%s:%d\t'%s'\n", argv[2], line, nfa_sim->scanner->buffer);
        print_matches(nfa_sim->matches);
        list_clear(nfa_sim->matches);
//return;
      }
      reset_nfa_sim(nfa_sim);
//printf("LOAD NEXT LINE\n");
    }
    fclose(search_input);
  }
//printf("AT_BOL IS %s SET\n", (nfa_sim->scanner->ctrl_flags & AT_BOL_FLAG) ? "" : "NOT");
//printf("AT_EOL IS %s SET\n", (nfa_sim->scanner->ctrl_flags & AT_EOL_FLAG) ? "" : "NOT");
  parser_free(parser);
  free_scanner(scanner);

  if(nfa_sim) {
    free_nfa_sim(nfa_sim);
  }

  /*printf("\n")*/
  return 0;
}
