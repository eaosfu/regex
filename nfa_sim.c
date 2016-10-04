#include <stdlib.h>
#include "regex_parser.h"
#include "nfa_sim.h"
#include "nfa_sim.h"
#include "slist.h"
#include "token.h"
#include "misc.h"
#include "nfa.h"

#include <stdio.h>

int
get_states(NFA * nfa, List * lp)
{
  int found_accepting_state = 0;
  if(nfa->value.type & ~(NFA_SPLIT|NFA_EPSILON)) {
    if(nfa->value.type == NFA_ACCEPTING) {
      found_accepting_state = 1;
//printf("\t\tACCEPTING: nfa->value.type: %d vs. %d\n", nfa->value.type, NFA_ACCEPTING);
    }
    else if(nfa->value.type == NFA_BOL_ANCHOR) {
//printf("\t\tBOL ANCHOR: nfa->value.type: %d vs. %d\n", nfa->value.type, NFA_ACCEPTING);
    }
    else if(nfa->value.type == NFA_EOL_ANCHOR) {
//printf("\t\tEOL ANCHOR: nfa->value.type: %d vs. %d\n", nfa->value.type, NFA_ACCEPTING);
    }
    else if(nfa->value.type == NFA_RANGE) {
//printf("\t\tAPPENDING: RANGE\n");
    }
    else if(nfa->value.type == NFA_NGLITERAL) {
//printf("\t\tNEGATED LITREAL\n");
    }
    else {
//printf("\t\tAPPENDING: '%c', type was: %d\n", nfa->value.literal, nfa->value.type);
    }
    list_push(lp, nfa);
  }
  else {
    if(nfa->value.type & NFA_SPLIT) {
//printf("SPLIT\n");
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
  unsigned int mask = 0x001 << (offset(c));
  if(range[index(c)] & mask) {
//printf("LITERAL IN RANGE\n");
    return 1;
  }
//printf("LITERAL NOT IN RANGE\n");
   return 0;
}


int
match(NFA * nfa, int c)
{
  int ret = 0;
  switch(nfa->value.type) {
    case NFA_ANY: {
//printf("\tmatching DOT\n");
      ret = 1;
    } break;
    case NFA_RANGE: {
//printf("CHECK CHARCLASS for '%c'(%d): range[%u] & 0x%x\n", c, c, c/32 - 1, 0x001 << (c % 32 - 1));
      ret = is_literal_in_range(*(nfa->value.range), c);
    } break;
    case NFA_NGLITERAL: {
//printf("\tmatch: '%c' != '%c'\n", nfa->value.literal, c);
      ret = !(c == nfa->value.literal);
    } break;
    case NFA_ACCEPTING: break;
    default: {
//printf("\tnfa type: %d -- ", nfa->value.type);
//printf("match: '%c'(%lu) vs '%c'(%lu)\n", nfa->value.literal, nfa->value.literal, c, c);
      ret = (c == nfa->value.literal);
    } break;
  } 
  return ret;
}


void *
free_match_string(void * m)
{
  if(m) {
    free(m);
  }
}


Match *
new_matched_string(char * buffer, int start, int end, List * matches)
{
//printf("\t new matched string: start: %d, end: %d\n", start, end);
  Match * ret = NULL;
  if(matches->head != NULL) {
    // Extend a previous match
    ret = matches->head->data; 
    if(buffer == ret->buffer && (start <= ret->end + 1)) {
      ret->end = end;
      // We don't want to add a new match to the 'matches' list
      // so make sure we return NULL.
      ret = NULL;
      goto RETURN;
    }
  }
//printf("\tNEW MATCHED STRING: ");
  ret = xmalloc(sizeof * ret);
  ret->buffer = buffer;
  ret->start  = start;
  ret->end    = end;

RETURN:
  return ret;
}


NFASim *
new_nfa_sim(NFA * nfa, char * buffer)
{
  NFASim * sim = xmalloc(sizeof * sim);
  sim->state = START;
  sim->nfa = nfa;
  sim->buffer = buffer;
  sim->input_ptr = buffer;
  sim->match_start = 0;
  sim->state_set1 = new_list();
  sim->state_set2 = new_list();
  sim->matches = new_list();
  return sim;
}


void
nfa_sim_reset_buffer(NFASim * sim, char * buffer)
{
  sim->input_ptr = sim->buffer = buffer;
  sim->match_start = 0;
}


void
reset_nfa_sim(NFASim * sim)
{
  sim->match_start = (sim->input_ptr - sim->buffer);
  sim->state = START;
  list_clear(sim->state_set1);
  list_clear(sim->state_set2);
  get_states(sim->nfa->parent, sim->state_set1);
}


int
run_nfa(NFASim * sim)
{
#define MOVE(n) ((NFA *)(n)->data)->out2
  reset_nfa_sim(sim);
  ListItem * current_state;
  Match * m;
  int match_start = 0;
  int match_end   = 0;
  while(sim->input_ptr[0] != '\0') {
    current_state = sim->state_set1->head;
    for(int i = 0; i < sim->state_set1->size; ++i) {
      // update 'matches' list with the newly matched substring
      // on matching char and transition to Accepting state
      if(match(current_state->data, sim->input_ptr[0]) &&
         get_states(MOVE(current_state), sim->state_set2)) {
          if((m = new_matched_string(sim->buffer, match_start, match_end, sim->matches))) {
            list_push(sim->matches, m);
          }
      }
      current_state = current_state->next;
    }

    if(sim->state_set2->size == 0) {
      if(sim->state == START) {
        sim->input_ptr++;
      }
      reset_nfa_sim(sim);
      match_start = match_end = sim->input_ptr - sim->buffer;
    }
    else {
      sim->state = MATCHING;
      list_swap(sim->state_set1, sim->state_set2);
      list_clear(sim->state_set2);
      sim->input_ptr++;
      match_end += 1;
    }
  }
#undef MOVE
  return SUCCESS;
}


void
print_matches(List * match_list)
{
  ListItem * m = match_list->head = list_reverse(match_list->head);
//printf("MATCH FOUND IN INPUT: %c\n", (((Match *)(m->data))->buffer)[0]);
  for(int i = 0; i < match_list->size; ++i, m = m->next) {
    int start = ((Match *)m->data)->start; 
    int end = ((Match *)m->data)->end;
    int match_len = end - start + 1;
printf("col: %d len: %d match: ", start+1, match_len);
//printf("START: %d\n", start);
//printf("END: %d\n", end);
//printf("len: %d\n'", match_len);
    for(int j = 0; j < match_len; j++) {
printf("%c", (((Match *)(m->data))->buffer)[j+start]);
    }
printf("\n");
  }
}


void
free_nfa_sim(NFASim* nfa_sim)
{
  list_free(nfa_sim->state_set1, NULL);
  list_free(nfa_sim->state_set2, NULL);
  list_free(nfa_sim->matches, free_match_string);
  free(nfa_sim);
}


int
main(int argc, char ** argv)
{
  Parser * parser;
  NFASim * nfa_sim = NULL;

  if(argc >= 2) {
    printf("Parsing file: %s\n", argv[1]);
    parser = init_parser(fopen(argv[1], "r"));
  }
  else {
    parser = init_parser(stdin);
  }

  printf("--> PHASE1: PARSING/COMPILING regex\n\n");
  parse_regex(parser);

  if(parser->err_msg_available) {
    printf("%s\n", parser->err_msg);
  }
  else if(argc > 2) {
    FILE * search_input = fopen(argv[2], "r");
    char * buffer;
    size_t buffer_len = 0;
    int line = 0;

    printf("\n--> RUNNING NFA SIMULAITON\n\n");
    nfa_sim = new_nfa_sim(peek(parser->symbol_stack), buffer);
    while(getline(&buffer, &buffer_len, search_input) != EOF) {
      ++line;
      nfa_sim_reset_buffer(nfa_sim, buffer);
      run_nfa(nfa_sim);
      if(nfa_sim->matches->size) {
        printf("%s:%d\t%s", argv[2], line, nfa_sim->buffer);
        //printf("line: %d ", line);
        printf("\t"); print_matches(nfa_sim->matches);
        list_clear(nfa_sim->matches);
      }
      reset_nfa_sim(nfa_sim);
//printf("LOAD NEXT LINE\n");
    }
    free(buffer);
  }
  else {
    //char * target = "HELLo HOW ARRRE YOU TODAY\?";
    //char * target = "weeknights";
    //char * target = "weekend";
    //char * target = "wek";
    //char * target = "bleek weeeknights";
    char * target = "This is a <EM>first (1st) </EM> test";
    //char * target = "abbbc";

    printf("\n--> RUNNING NFA SIMULAITON\n\n");

    //NFA * nfa = pop(parser->symbol_stack);
    //NFASim * nfa_sim = new_nfa_sim(nfa, target);

    nfa_sim = new_nfa_sim(peek(parser->symbol_stack), target);
    run_nfa(nfa_sim);
    if(nfa_sim->matches->size) {
      printf("Match found!\n");
      print_matches(nfa_sim->matches);
      //print_matches(nfa_sim->back_references);
    }
    else {
      printf("MATCH FAILED\n");      
    }

  }

  parser_free(parser);
  if(nfa_sim) {
    free_nfa_sim(nfa_sim);
  }

  printf("\n");
  return 0;
}
