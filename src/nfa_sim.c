#include <stdlib.h>
#include "scanner.h"
#include "nfa_sim.h"
#include "nfa_sim.h"
#include "slist.h"
#include "token.h"
#include "misc.h"
#include "nfa.h"
#include "scanner.h"

#include <string.h>
#include <stdio.h>

#define CTRL_FLAGS(s) (s)->parser->scanner->ctrl_flags

int
get_states(NFA * nfa, List * lp)
{
  int found_accepting_state = 0;
  if(nfa->value.type & ~(NFA_SPLIT|NFA_EPSILON)) {
    if(nfa->value.type == NFA_ACCEPTING) {
      found_accepting_state = 1;
    }
    else {
      list_push(lp, nfa);
    }
  }
  else {
    if(nfa->value.type & NFA_SPLIT) {
      found_accepting_state += get_states(nfa->out1, lp);
    }
    found_accepting_state += get_states(nfa->out2, lp);
  }
  return found_accepting_state;
}


static inline int
is_literal_in_range(nfa_range range, unsigned int c)
{
  #define index(c)  ((c) / 32)
  #define offset(c) ((c) % 32)
  unsigned int mask = 0x01 << (offset(c));
  if(range[index(c)] & mask) {
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
static unsigned int new_match_count = 0;
  match = xmalloc(sizeof * match);
  match->buffer = buffer;
  match->start  = start;
  match->end    = end;

  list_push(matches, match);

#undef BUFFER
RETURN:
  return match;
  
}


NFASim *
new_nfa_sim(Parser * parser)
{
  NFASim * sim = xmalloc(sizeof * sim);
  sim->parser = parser;
  sim->nfa = peek(parser->symbol_stack);
  sim->ctrl_flags = parser->scanner->ctrl_flags;
  sim->state_set1 = new_list();
  sim->state_set2 = new_list();
  sim->matches = new_list();
  return sim;
}


void
reset_nfa_sim(NFASim * sim)
{
  list_clear(sim->state_set1);
  list_clear(sim->state_set2);
  get_states(sim->nfa->parent, sim->state_set1);
}


int
run_nfa(NFASim * sim)
{
#define GLOBAL_MATCH(s) (CTRL_FLAGS(s) & MGLOBAL_FLAG)
#define CONTINUE_MATCHING(s)                            \
  ((GLOBAL_MATCH(s)) ? 1 : (MATCH_COUNT(s) == 1) ? 0 : 1)

#define NEXT_STATE(n) ((NFA *)(n)->data)->out2
#define MATCH_COUNT(s) ((s)->matches->size)
#define MATCH_FAILED(s) (((s)->state_set2->size == 0) ? 1 : 0)
#define MATCH_LINE(s) 0 // replace this with the above line when MATCH_LINE_FLAG has been added

  reset_nfa_sim(sim);
  ListItem * current_state;
  Match * m;
  int match_start = 0;
  int match_end   = 0;
  int c = next_char(sim->parser->scanner);
  int accept = 0;
  int eol_adjust = 0;
  int new_match = 1;
  NFA * nfa;

  List * tmp = new_list();

  while(c != '\0') {
    current_state = sim->state_set1->head;
    for(int i = 0; i < sim->state_set1->size; ++i) {
      nfa = current_state->data;
      accept = 0;
      eol_adjust = 0;
      switch(nfa->value.type) {
        case NFA_ANY: {
          if(c != sim->parser->scanner->eol_symbol) {
            accept = get_states(NEXT_STATE(current_state), sim->state_set2);
          }
        } break;
        case NFA_RANGE: {
          if(c != sim->parser->scanner->eol_symbol) {
            if(is_literal_in_range(*(nfa->value.range), c)) {
              accept = get_states(NEXT_STATE(current_state), sim->state_set2);
            }
          }
        } break;
        case NFA_BOL_ANCHOR: {
          if(CTRL_FLAGS(sim) & AT_BOL_FLAG) {
          // need to extend state_set1
          // GET RID OF THE TMP list
            accept = get_states(NEXT_STATE(current_state), tmp);
            if(tmp->size > 0) {
              list_append(sim->state_set1, tmp->head->data);
              list_clear(tmp);
            }
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
            accept = get_states(NEXT_STATE(current_state), sim->state_set2);
          }
        } break;
      }

      if(accept) {
        record_match(sim->parser->scanner->buffer,
                     sim->matches, 
                     sim->parser->scanner->str_begin,
                     sim->parser->scanner->readhead - 1 - eol_adjust,
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
      reset_nfa_sim(sim);
      ++match_start;
      restart_from(sim->parser->scanner, (sim->parser->scanner->buffer + match_start));
        c = next_char(sim->parser->scanner);
      match_end = match_start;
      new_match = 1;
    }
    else {
      list_swap(sim->state_set1, sim->state_set2);
      list_clear(sim->state_set2);
      match_end += 1;
      c = next_char(sim->parser->scanner);
    }
  }

  list_free(tmp, NULL);

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
  list_free(nfa_sim->state_set1, NULL);
  list_free(nfa_sim->state_set2, NULL);
  //list_free(nfa_sim->matches, free_match_string);
  list_free(nfa_sim->matches, NULL);
  free(nfa_sim);
}


int
main(int argc, char ** argv)
{
  Parser * parser   = NULL;
  NFASim * nfa_sim  = NULL;
  Scanner * scanner = new_scanner();
  FILE * file;

  if(argc >= 2) {
    /*printf("Parsing file: %s\n", argv[1])*/
    file = fopen(argv[1], "r");
    if(file == NULL) {
      fatal("UNABLE OPEN REGEX FILE\n");
    }
    char * buffer = NULL;
    unsigned long int buf_len = 0;
    unsigned int line_len = getline(&buffer, &buf_len, file);
    init_scanner(scanner, buffer, buf_len, line_len);
/*printf("REGEX: '%s'\n", scanner->buffer)*/

//check_match_anchors(scanner);
    if(scanner->line_len < 0) {
      fatal("UNABLE READ REGEX FILE\n");
    }
    parser = init_parser(scanner);
  }
  else {
    fatal("NO INPUT FILE PROVIDED\n");
  }

  /*printf("--> PHASE1: PARSING/COMPILING regex\n\n")*/
//printf("EOL_FAG %s\n", (scanner->ctrl_flags & EOL_FLAG) ? "SET" : "NOT SET");
  parse_regex(parser);
  fclose(file);

  //if(scanner->ctrl_flags & EOL_FLAG) {
/*
  if(REVERSE(scanner->ctrl_flags)) {
    SET_REVERSE_FLAG(&scanner->ctrl_flags);
printf("SET SCANNER REVERSE\n");
  }
*/
  if(parser->err_msg_available) {
    /*printf("%s\n", parser->err_msg)*/
  }
  else if(argc > 2) {
    FILE * search_input = fopen(argv[2], "r");

    if(search_input == NULL) {
      fatal("Unable to open file\n");
    }
    int line = 0;

    /*printf("\n--> RUNNING NFA SIMULAITON\n\n")*/
    nfa_sim = new_nfa_sim(parser);
SET_MGLOBAL_FLAG(&scanner->ctrl_flags);
//printf("MGLOBAL MATCH IS %s SET\n", (scanner->ctrl_flags & MGLOBAL_FLAG) ? "": "NOT");
    while((scanner->line_len = getline(&scanner->buffer, &scanner->buf_len, search_input)) > 0) {
      ++line;
      scanner->line_no = line;
      reset_scanner(scanner);
///*printf("NEW BUFFER:%d %d, '%s'\n", line, scanner->line_len, nfa_sim->parser->scanner->buffer)*/
      run_nfa(nfa_sim);
      if(nfa_sim->matches->size) {
//printf("%s:%d\t'%s'\n", argv[2], line, nfa_sim->parser->scanner->buffer);
        print_matches(nfa_sim->matches);
        list_clear(nfa_sim->matches);
//return;
      }
      reset_nfa_sim(nfa_sim);
//printf("LOAD NEXT LINE\n");
    }
    fclose(search_input);
  }
//printf("AT_BOL IS %s SET\n", (nfa_sim->parser->scanner->ctrl_flags & AT_BOL_FLAG) ? "" : "NOT");
//printf("AT_EOL IS %s SET\n", (nfa_sim->parser->scanner->ctrl_flags & AT_EOL_FLAG) ? "" : "NOT");
  parser_free(parser);
  if(nfa_sim) {
    free_nfa_sim(nfa_sim);
  }

  /*printf("\n")*/
  return 0;
}
