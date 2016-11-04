#include <stdlib.h>
#include "scanner.h"
#include "slist.h"
#include "token.h"
#include "misc.h"
#include "nfa.h"
#include "scanner.h"
#include "recognizer.h"

#include <string.h>
#include <stdio.h>

//#define CTRL_FLAGS(s) (*((s)->ctrl_flags))

// TESTING
void
print_backref_match(NFASim * sim, int id, int shorten)
{
  char * capture_group_match = (sim->backref_matches)[id].start;
  char * end = (sim->backref_matches)[id].end;
  while(capture_group_match <= end) {
    printf("%c", capture_group_match[0]);
    ++capture_group_match;
  }
printf("\n");
}
// END TESTING


void
record_capturegroup_match(NFASim * sim, unsigned int capturegroup_idx, unsigned int flag, int iteration)
{
  if(flag == NFA_CAPTUREGRP_BEGIN) {
    sim->backref_matches[capturegroup_idx].buffer = sim->scanner->buffer;
    if(iteration == 0) {
      sim->backref_matches[capturegroup_idx].start = get_cur_pos(sim->scanner);
    }
    else {
      sim->backref_matches[capturegroup_idx].start = get_scanner_readhead(sim->scanner);
    }
    // set end to NULL so when we process a backreference we can tell whether or
    // not the associated capture group matched.
    sim->backref_matches[capturegroup_idx].end = NULL;
  }
  else {
    // if we reach this point all that remains is to mark where the match ends.
    sim->backref_matches[capturegroup_idx].end = get_cur_pos(sim->scanner);

    //sim->backref_matches[capturegroup_idx].iteration = iteration;
/*
char * tmp = sim->backref_matches[capturegroup_idx].start;
printf("BACKREF[%d] EXPANDED TO MATCH: ", capturegroup_idx + 1);
while(tmp <= sim->backref_matches[capturegroup_idx].end)
{
  printf("%c", tmp[0]);
  ++tmp;
}
printf("\n");
*/
  }

  return;
}


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
//printf("CHECK ID: %d vs. WORKING SET: %d -- [index: %d -- offset: %d] %d\n", id, 
//  UINT_BITS * index(id) + offset(id), 
//  index(id), offset(id),
//  (((sim->stateset2_ids)[index(id)] & (0x01 << offset(id))) == 0) ? 0 : 1);
  return ((sim->stateset2_ids)[index(id)] & (0x01 << offset(id)));
#undef index
#undef offset
}


void
add_id_to_stateset2(NFASim * sim, unsigned int id)
{
#define index(id)  ((id) / UINT_BITS)
#define offset(id) (((id) % UINT_BITS) - 1)
  (sim->stateset2_ids)[index(id)] |= (0x01 << (id) ? offset(id) : 0);
#undef index
#undef offset
  return;
}


int
get_states(NFASim * sim, NFA * nfa, List * lp, int iteration)
{
  int found_accepting_state = 0;
  if(nfa->value.type & ~(NFA_SPLIT|NFA_EPSILON|NFA_CAPTUREGRP_BEGIN|NFA_CAPTUREGRP_END)) {
    if(nfa->value.type & NFA_ACCEPTING) {
      found_accepting_state = 1;
    }
    else if(nfa->value.type & NFA_BACKREFERENCE) {
        list_append(lp, nfa);
    }
    else {
      if(CTRL_FLAGS(sim) & MARK_STATES_FLAG) {
        if(id_in_stateset2(sim, nfa->id) == 0) {
          add_id_to_stateset2(sim, nfa->id);
          list_append(lp, nfa);
        }
      }
      else {
        list_append(lp, nfa);
      }
    }
  }
  else {
    if(nfa->value.type & NFA_SPLIT) {
      found_accepting_state += get_states(sim, nfa->out1, lp, iteration);
    }
    else if(nfa->value.type & (NFA_CAPTUREGRP_BEGIN|NFA_CAPTUREGRP_END)) {
      record_capturegroup_match(sim, nfa->id, nfa->value.type, iteration);
    }
    found_accepting_state += get_states(sim, nfa->out2, lp, iteration);
  }
  return found_accepting_state;
}


static inline int
is_literal_in_range(nfa_range range, unsigned int c)
{
  #define index(c)  ((c) / 32)
  #define offset(c) ((c) % 32)
  unsigned int mask = 0x01 << (offset(c)); if(range[index(c)] & mask) {
//if(c == '.')
//  printf("LITERAL '%c' IN RANGE\n", c);
    return 1;
  }
//if(c == '.')
//  printf("LITERAL '%c' NOT IN RANGE\n", c);
   return 0;
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
  if(matches->head != NULL && buffer == MATCH_BUFFER(matches) && !new_match) {
    // Extend a previous match
    match = matches->head->data;
    if((start - match->end) <= 1) {
      match->end = end;
      // We don't want to add a new match to the 'matches' list
      // so make sure we return NULL.
      match = NULL;
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
  sim->matches = new_list();
  return sim;
}


void
reset_nfa_sim(NFASim * sim, int iteration)
{
//printf("\treset nfa sim\n");
  list_clear(sim->state_set1);
  list_clear(sim->state_set2);
//printf("WORKING SET ID BITS: %d\n", WORKING_SET_ID_BITS);
  clear_stateset2_ids(sim);
  CLEAR_MARK_STATES_FLAG(&CTRL_FLAGS(sim));
  get_states(sim, sim->nfa->parent, sim->state_set1, iteration);
//fatal("DONE HERE\n");
  SET_MARK_STATES_FLAG(&CTRL_FLAGS(sim));
}


int
run_nfa(NFASim * sim)
{
#define GLOBAL_MATCH(s) (CTRL_FLAGS(s) & MGLOBAL_FLAG)
#define CONTINUE_MATCHING(s)                            \
  ((GLOBAL_MATCH(s)) ? 1 : (MATCH_COUNT(s) == 1) ? 0 : 1)

#define NEXT_STATE(n) ((NFA *)(n)->data)->out2
#define PARENT_STATE(n) ((NFA *)(n)->data)->parent
#define MATCH_COUNT(s) ((s)->matches->size)
#define MATCH_FAILED(s) (((s)->state_set2->size == 0) ? 1 : 0)
#define MATCH_LINE(s) 0 // replace this with the above line when MATCH_LINE_FLAG has been added


#define BACKREF_MATCH_LEN(sim, idx) \
  (((sim)->backref_matches)[(idx)].end - ((sim)->backref_matches)[(idx)].start)


  ListItem * current_state;
  int iteration = 0;
  int match_start = 0;
  int match_end   = 0;
  int c = next_char(sim->scanner);
  int accept = 0;
  int eol_adjust = 0;
  int new_match = 1;
  NFA * nfa;

  reset_nfa_sim(sim, iteration);

  while(c != '\0') {
    ++iteration;
    current_state = sim->state_set1->head;
    for(int i = 0; i < sim->state_set1->size; ++i) {
      nfa = current_state->data;
      accept = 0;
      eol_adjust = 0;
      switch(nfa->value.type) {
        case NFA_INTERVAL: {
          ++(nfa->value.count);
//printf("min: %d\n", nfa->value.min_rep);
//printf("max: %d\n", nfa->value.max_rep);
//printf("count: %d\n", nfa->value.count);
          if(nfa->value.count < nfa->value.min_rep
          || nfa->value.count < nfa->value.max_rep) {
//printf("JUMP BACK TO START NODE: 0x%x\n", PARENT_STATE(current_state));
            accept = 1;
            get_states(sim, PARENT_STATE(current_state), sim->state_set1, iteration);
          }
          else {
            nfa->value.count = 0;
            accept = get_states(sim, NEXT_STATE(current_state), sim->state_set2, iteration);
//printf("ACCEPT?:%d\n", accept);
          }
        } break;
        case NFA_BACKREFERENCE: {
          int br_match_status = 0;
          // check if capture group has matched anything
          if(((sim->backref_matches)[nfa->id - 1].end) == 0) {
//printf("BREAKING!\n");
            break;
          }

          char * capture_group_match = (sim->backref_matches)[nfa->id - 1].start;
          char * capture_group_match_end = (sim->backref_matches)[nfa->id - 1].end;
/*
          if((sim->backref_matches)[nfa->id - 1].iteration == iteration) {
//printf("END CAPTURE GROUP AT: %s\n", capture_group_match_end);
            --capture_group_match_end;
//printf("END CAPTURE GROUP AT: %s\n", capture_group_match_end);
          }
*/
          char * tmp_input = get_cur_pos(sim->scanner);
          int br_idx = 0;
//printf("-- remaining input: %s vs tmp_input: %s\n", get_cur_pos(sim->scanner), tmp_input);
//print_backref_match(sim, nfa->id - 1, 1);
          while(capture_group_match <= capture_group_match_end) {
            if(tmp_input[0] == sim->scanner->eol_symbol || tmp_input[0] != capture_group_match[0]) {
//printf("BREAK ON: %c -- %c\n",capture_group_match[0], tmp_input[0]);
              br_match_status = 0;
              break;
            }
//printf("%c -- %c\n", capture_group_match[0], tmp_input[0]);
            ++br_idx;
            ++capture_group_match;
            ++tmp_input;
            br_match_status = 1;
//printf("next: %c -- %c\n", capture_group_match[0], tmp_input[0]);
          }
          if(br_match_status > 0) {
//printf("tmp_input: '%c' -- capture group: '%c'\n", tmp_input[0], capture_group_match);
            accept = get_states(sim, NEXT_STATE(current_state), sim->state_set2, iteration);
            sim->scanner->readhead = tmp_input;
            match_end += br_idx - 1;
//printf("\tremaining input: %s\n", get_scanner_readhead(sim->scanner));
//printf("did we accept?: %d\n\n", accept);
          }
        } break;
        case NFA_ANY: {
          if(c != sim->parser->scanner->eol_symbol) {
            accept = get_states(sim, NEXT_STATE(current_state), sim->state_set2, iteration);
//printf("\n");
          }
        } break;
        case NFA_RANGE: {
          if(c != sim->parser->scanner->eol_symbol) {
            if(is_literal_in_range(*(nfa->value.range), c)) {
//printf("%c IS IN RANGE -- ", c);
              accept = get_states(sim, NEXT_STATE(current_state), sim->state_set2, iteration);
//printf("\n");
//printf("ACCEPT: %d, SIZE OF STATE_SET2: %d\n", accept, sim->state_set2->size);
            }
          }
        } break;
        case NFA_BOL_ANCHOR: {
          if(CTRL_FLAGS(sim) & AT_BOL_FLAG) {
            accept = get_states(sim, NEXT_STATE(current_state), sim->state_set1, iteration);
          }
        } break;
        case NFA_EOL_ANCHOR: {
          if(c == sim->parser->scanner->eol_symbol) {
            eol_adjust = 1;
            accept = 1;
          }
        } break;
        default: {
          if(c == nfa->value.literal) {
//printf("%c MATCH -- %c", c, nfa->value.literal);
            accept = get_states(sim, NEXT_STATE(current_state), sim->state_set2, iteration);
//printf("\n");
//printf("ACCEPT: %d\n", accept);
          }
        } break;
      }

//print_backref_match(sim, 2);
      if(accept) {
        record_match(sim->scanner->buffer,
                     sim->matches, 
                     sim->scanner->str_begin,
                     //get_scanner_readhead(sim->scanner) - 1 - eol_adjust,
                     get_cur_pos(sim->scanner) - eol_adjust,
                     new_match);
        match_start = match_end;
        new_match = 0;
      }

      // load the next item in state_set1
      current_state = current_state->next;
    }

    if(MATCH_FAILED(sim)) {
      if(CONTINUE_MATCHING(sim) == 0) {
        break;
      }
//printf("\tRESET ON: %c\n", c);
      reset_nfa_sim(sim, iteration);
      ++match_start;
      restart_from(sim->parser->scanner, (sim->parser->scanner->buffer + match_start));
      c = next_char(sim->parser->scanner);
//printf("NEXT CHAR AFTER RESET: %c\n", c);
      match_end = match_start;
      new_match = 1;
//printf("\n");
    }
    else {
      list_swap(sim->state_set1, sim->state_set2);
      list_clear(sim->state_set2);
      clear_stateset2_ids(sim);
//printf("\n");
      match_end += 1;
      c = next_char(sim->parser->scanner);
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
//printf("NEW BUFFER:%d %d, '%s'\n", line, scanner->line_len, nfa_sim->parser->scanner->buffer);
//#include<unistd.h>
//sleep(1);
      run_nfa(nfa_sim);
      if(nfa_sim->matches->size) {
//printf("%s:%d\t'%s'\n", argv[2], line, nfa_sim->parser->scanner->buffer);
        print_matches(nfa_sim->matches);
        list_clear(nfa_sim->matches);
//return;
      }
      reset_nfa_sim(nfa_sim, 0);
//printf("LOAD NEXT LINE\n");
    }
    fclose(search_input);
  }
//printf("AT_BOL IS %s SET\n", (nfa_sim->parser->scanner->ctrl_flags & AT_BOL_FLAG) ? "" : "NOT");
//printf("AT_EOL IS %s SET\n", (nfa_sim->parser->scanner->ctrl_flags & AT_EOL_FLAG) ? "" : "NOT");
  parser_free(parser);
  free_scanner(scanner);

  if(nfa_sim) {
    free_nfa_sim(nfa_sim);
  }

  /*printf("\n")*/
  return 0;
}
