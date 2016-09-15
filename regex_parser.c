#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"
#include "token.h"
#include "nfa.h"
#include "stack.h"
#include "scanner.h"
//#include "charset.h"

#include <stdio.h>

#define EMPTY_COLLATION_CLASS_ERROR \
"Malformed bracket expression. Correct syntax for a collation expression is: " \
"'[[.<matching list>.]]'.\nOr rewrite bracket expression as '[.[]]' to match" \
" any of the characters: '.[]'\n"

typedef struct IntervalRecord {
  NFA * node;
  unsigned int min;
  unsigned int max;
} IntervalRecord;


typedef struct Parser {
  int err_msg_available;
  char err_msg[50];
  Token lookahead;
  Stack * symbol_stack;
  Stack * interval_stack;
  Scanner * scanner;
} Parser;


static void
parser_assert(void * p)
{
  if(p == NULL) {
    fatal("PARSER ASSERT FAIL\n");
  }
}


static inline void
clear_err_msg(Parser * parser)
{
  memset(parser->err_msg, 0, 50);
  parser->err_msg_available = 0;
}


void
set_err_msg(Parser * parser, char * msg)
{
  clear_err_msg(parser);
  strncpy(parser->err_msg, msg, 49);
  parser->err_msg_available = 1;
}


IntervalRecord *
new_interval_record(NFA * nfa, unsigned int min, unsigned int max)
{
  IntervalRecord * ir = xmalloc(sizeof * ir);
  ir->node = nfa;
  ir->min = min;
  ir->max = max;
  return ir;
}


void parse_paren_expression(Parser * parser);
void parse_literal_expression(Parser * parser);
void parse_quantifier_expression(Parser * parser);
void parse_regex(Parser * parser);
void parse_sub_expression(Parser * parser);
void parse_bracket_expression(Parser * parser);

static inline void
parser_consume_token(Parser * parser)
{
  parser->lookahead = *scan(parser->scanner);
}

static inline void
parser_backtrack(Parser * parser, Token * lookahead_reset)
{
  unput(parser->scanner);
  parser->lookahead = *(lookahead_reset);
}


Parser *
init_parser(FILE * input_stream)
{
printf("PARSER INIT START:\n");
  Parser * parser        = xmalloc(sizeof(*parser));
  parser->scanner        = init_scanner(input_stream);
  parser->symbol_stack   = new_stack();
  parser->interval_stack = new_stack();
  parser_consume_token(parser);
printf("PARSER INIT END:\n");
  return parser;
}


void
parse_quantifier_expression(Parser * parser)
{
  static int quantifier_count = 0;
  NFA * nfa;
  switch(parser->lookahead.type) {
    case PLUS: {
      if(quantifier_count >= 1) {
        fatal("Invalid use of consecutive 'multi' operators\n");
      }
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
printf("POS CLOSURE: RANGE_NFA: 0x%x\n", nfa);
      push(parser->symbol_stack, new_posclosure_nfa(nfa));
      quantifier_count++;
      parse_quantifier_expression(parser);
      quantifier_count--;
    } break;
    case KLEENE: {
      if(quantifier_count > 1) {
        fatal("Invalid use of consecutive 'multi' operators\n");
      }
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      push(parser->symbol_stack, new_kleene_nfa(nfa));
      quantifier_count++;
      parse_quantifier_expression(parser);
      quantifier_count--;
    } break;
    case OPENBRACE: {
      // handle interval expression like in '<expression>{m,n}'
#define DISCARD_WHITESPACE(p) \
  do {                                                                    \
    while((p)->lookahead.value == ' ' || (p)->lookahead.value == '\t') {  \
      parser_consume_token((p));                                          \
    }                                                                     \
  } while(0)

#define CONVERT_TO_UINT(p, interval)                                      \
  do {                                                                    \
    while((p)->lookahead.type == ASCIIDIGIT) {                            \
      dec_pos *= 10;                                                      \
      (interval) = ((interval) * dec_pos) + ((p)->lookahead.value - '0'); \
      dec_pos++;                                                          \
      parser_consume_token((p));                                          \
    }                                                                     \
  } while(0)

      if(quantifier_count > 1) {
        fatal("Invalid use of consecutive 'multi' operators\n");
      }

      parser_consume_token(parser);

      int set_max = 0;
      int dec_pos = 0;
      unsigned int min = 0;
      unsigned int max = 0;
      IntervalRecord * interval_record;

      DISCARD_WHITESPACE(parser);
      CONVERT_TO_UINT(parser, min);
      DISCARD_WHITESPACE(parser);

      // Note we use lookahead.value instead of lookahead.type
      // since the scanner never assigns special meaning to ','
      if(parser->lookahead.value == ',') {
        dec_pos = 0;
        parser_consume_token(parser);
        DISCARD_WHITESPACE(parser);
        CONVERT_TO_UINT(parser, max);
        DISCARD_WHITESPACE(parser);
        set_max = 1;
      }

printf("MIN: %d, MAX: %d\n", min, max);

      if(parser->lookahead.type == CLOSEBRACE) {
        if(set_max) {
          if((max != 0) && (min > max)) {
            fatal("Invalid interval, min > max\n");
          }
          if(min == 0 && max == 0) {
            // <expression>{,} ;create a kleenes closure
            push(parser->symbol_stack, new_kleene_nfa(pop(parser->symbol_stack)));
          }
          else if(min == 0 && max == 1) {
            // <expression>{0,1} ; equivalent to <expression>?
            push(parser->symbol_stack, new_qmark_nfa(pop(parser->symbol_stack)));
          }
          else if(min == 1 && max == 0) {
            // <expression>{1,} ; equivalent to <expression>+
            push(parser->symbol_stack, new_posclosure_nfa(pop(parser->symbol_stack)));
          }
          else {
            if(min > 0 && max == 0) {
              // <expression>{Min,} ;match at least Min, at most Infinity
              push(parser->symbol_stack, new_kleene_nfa(pop(parser->symbol_stack)));
            }
            // <expression>{,Max} ;match between 0 and Max
            // <expression>{Min,Max} ;match between Min and Max
            interval_record = new_interval_record(peek(parser->symbol_stack), min, max);
            push(parser->interval_stack, interval_record);
          }
        }
        else {
          if(min > 0) {
            // <expression>{M}
            interval_record = new_interval_record(peek(parser->symbol_stack), min, max);
            push(parser->interval_stack, interval_record);
          }
          else {
            // if we hit this point then min == 0 and this is
            // essentially a 'don't match' operation so pop the nfa
            // off of the symbol_stack
            pop(parser->symbol_stack);
          }
        }
      }
      else {
        fatal("Syntax error at interval expression. Expected '}'\n");
      }
#undef DISCARD_WHITESPACE
#undef CONVERT_TO_UINT
      quantifier_count++;
      parse_quantifier_expression(parser);
      quantifier_count--;
    } break;
    case QMARK: {
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      push(parser->symbol_stack, new_qmark_nfa(nfa));
      parse_quantifier_expression(parser);
    } break;
    case PIPE: {
printf("\tPIPE!!!\n\n");
      parser_consume_token(parser);
      // concatenate everything on the stack unitl we see an open paren
      // or until nothing is left on the stack
      NFA * left = NULL;
      NFA *right = NULL;
      while(parser->symbol_stack->size > 1) {
        right = pop(parser->symbol_stack);
        left  = pop(parser->symbol_stack);
        if(left == NULL) {
printf("LEFT PAREN POPPED: symbol stack top: 0x%x\n", parser->symbol_stack->head);
          push(parser->symbol_stack, right);
          break;
        }
        // adjust our count of number of expressions seen so far
        push(parser->symbol_stack, concatenate_nfa(left, right));
      }
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, new_literal_nfa(0, NFA_EPSILON));
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      push(parser->symbol_stack, new_alternation_nfa(left, right));
printf("ALTERNATION OUT1: %d\n", ((NFA *)peek(parser->symbol_stack))->parent->out1->value.type);
printf("ALTERNATION OUT2: %d\n", ((NFA *)peek(parser->symbol_stack))->parent->out2->value.type);
    } break;
    case CLOSEPAREN: // epsilon production
printf("CLOSEPAREN\n");
    default: {       // epsilon production
      break;
    }
  }
}


int
token_in_charclass(unsigned int element, int reset)
{
  int found = 0;
  static int used_charclass_slots = 0;
  static unsigned int charclass[128] = {0};

  if(reset) {
    //memset((void*)&charclass, 0, 128);
    for(int i = 0; i < 128; ++i) {
      charclass[i] = 0;
    }
    used_charclass_slots = 0;
    return 0;
  }

  int i = 0;
  for(; i < used_charclass_slots; ++i) {
    if(element == charclass[i]) {
      found = 1;
printf("\%c(t0x%x) FOUND IN CHARCLASS AT idx: %d = 0x%x\n", element, element, i, charclass[i]);
      break;
    }
  }

  if(!found) {
printf("\t\t%c(0x%x) INSERTED INTO CHARCLASS AT idx: %d = 0x%x\n", element, element, i, charclass[i]);
//#include <unistd.h>
//sleep(2);
    charclass[i] = element;
    used_charclass_slots++;
  }

  return found;
}


// WE NOW USE A SINGLE RANGE NFA INSTANTIATED IN CALLER AND THEN JUST
// UPDATE THE BIT FIELD APPROPRIATELY. 
// STILL NEED TO UPDATE EXPRESSIONS LIKE [.<expression>.], etc. to 
// so they use the shared range nfa
void
parse_matching_list(Parser * parser, NFA * range_nfa, int negate)
{
#define new_mlliteral_nfa(token, negate)                       \
  ((negate)) ? new_literal_nfa((token).value, NFA_NGLITERAL)   \
             : new_literal_nfa((token).value, 0)

#define update_mlliteral_type(nfa_node, negate)           \
  ((negate)) ? ((nfa_node)->value.type = NFA_NGLITERAL)   \
             : 0 // do nothing

printf("PARSE MATCHING LIST: parser->lookahead: %c\n", parser->lookahead.value);
  
  NFA * open_delim_p;
  char found = 0;
  unsigned int prev_token_val;
 
  Token prev_token = parser->lookahead;
  parser_consume_token(parser);
  Token lookahead = parser->lookahead;

  
  if(prev_token.type == __EOF) {
printf("HERE!!!\n");
    return;
  }

  if(prev_token.type == OPENBRACKET  && (lookahead.type == DOT 
                                          ||  lookahead.type == COLON 
                                          ||  lookahead.type == EQUAL)) {
    symbol_type delim = lookahead.type;
    prev_token = parser->lookahead;
    open_delim_p = new_literal_nfa(parser->lookahead.value, 0);
    // push the punctuation mark
    push(parser->symbol_stack, open_delim_p); 
    parser_consume_token(parser);

    // handle case where ']' immediately follows '[<delim> (e.x. '[.].]')
    if(parser->lookahead.type == CLOSEBRACKET) {
      prev_token = parser->lookahead;
      push(parser->symbol_stack, new_literal_nfa(parser->lookahead.value, 0));
      parser_consume_token(parser);
    }

    while(parser->lookahead.type != __EOF) {
      if(parser->lookahead.type == CLOSEBRACKET && prev_token.type == delim) {
        // pop DOT, COLON or EQUAL
        pop(parser->symbol_stack);
        break;
      }
      prev_token = parser->lookahead;
      push(parser->symbol_stack, new_literal_nfa(parser->lookahead.value, 0));
      parser_consume_token(parser);
    }

    if(parser->lookahead.type != CLOSEBRACKET && prev_token.type != delim) {
      fatal("Malformed bracket expression; missing '.]' or ':]' or '=]'\n");
    }
 
    NFA * left  = NULL;
    NFA * right = pop(parser->symbol_stack);
 
    // We've successfully parsed a collation class
    switch(delim) {
      case DOT: {
        while((left = pop(parser->symbol_stack)) != open_delim_p) {
          update_mlliteral_type(left->parent, negate);
          update_mlliteral_type(right->parent, negate);
          right = concatenate_nfa(left, right);
        }
// release open_delim_p
free(open_delim_p);
        push(parser->symbol_stack, right);
      } break;
      case COLON: {
        // handle expression like '[[:alpha:]]'
        // pop elements off the stack to form a string
        // check that string againgst POSIX character classes
        // if string doesn't match any of these, error out
        // create POSIX character class alternation nfa
      } break;
      case EQUAL: {
        // I've never used this so need to read up on how this is 
        // supposed to work...
      } break;
    }
    // discard the current ']' token
    parser_consume_token(parser);
  }
  else if(prev_token.type == CLOSEBRACKET ) {
    if(lookahead.type != CLOSEBRACKET) {
      // restore input buffer to how it was when entered this function
      parser_backtrack(parser, &prev_token);
      // let parse_bracket_expressoin() handle the error
      return;
    }
    if(!token_in_charclass(prev_token.value, 0)) {
      update_range_nfa(prev_token.value, prev_token.value, range_nfa, negate);
      //push(parser->symbol_stack, new_mlliteral_nfa(prev_token, negate));
    }
  }
  else if(lookahead.type == __EOF) {
    // If we hit this point we're missing the closing ']'
    // handle error in parse_bracket_expression()
    return;
  }
  else {//if(!token_in_charclass(prev_token.value, 0)) {
    NFA * left;
printf("HERE: prev_token: %c, lookahead: %c\n", prev_token.value, lookahead.value);
    if(lookahead.type == HYPHEN) {
      // process a range expression like [a-z], [a-Z], [A-Z], etc.
      int range_invalid = 1;
      parser_consume_token(parser); // consume hyphen
      lookahead = parser->lookahead;

      // Note that we accept the range [A-z]
      if(prev_token.value >= 'a' && prev_token.value <= 'z') {
        if(lookahead.value >= 'A' && lookahead.value <= 'Z') {
           //left = new_range_nfa(prev_token.value, 'z', negate);
           //update_range_nfa('A', lookahead.value, left->parent->value.range, negate);
           update_range_nfa('A', lookahead.value, range_nfa, negate);
           range_invalid = 0;
        }
        else if(lookahead.value >= prev_token.value && lookahead.value <= 'z') {
          //left = new_range_nfa(prev_token.value, lookahead.value, negate);
          update_range_nfa(prev_token.value, lookahead.value, range_nfa, negate);
           range_invalid = 0;
        }
      }
      else if((prev_token.value >= 'A' && prev_token.value <= 'Z') && 
              (lookahead.value >= prev_token.value && lookahead.value <= 'Z')) {
        //left = new_range_nfa('A', 'Z', negate);
        update_range_nfa('A', 'Z', range_nfa, negate);
        range_invalid = 0;
      }
      else if(prev_token.value <= lookahead.value) {
        //left = new_range_nfa(prev_token.value, lookahead.value, negate);
        update_range_nfa(prev_token.value, lookahead.value, range_nfa, negate);
        range_invalid = 0;
      }
      
      if(range_invalid) {
        fatal("Invalid range expression '[low_bound, high_bound]'"
              " with low_bound > high_bound\n");
      }

      parser_consume_token(parser); // consume end bound
      //push(parser->symbol_stack, left);
    }
    else if(!token_in_charclass(prev_token.value, 0)) {
      update_range_nfa(prev_token.value, prev_token.value, range_nfa, negate);
      //left = new_mlliteral_nfa(prev_token, negate);
      //push(parser->symbol_stack, left);
    }
  }

  parse_matching_list(parser, range_nfa, negate);

#undef new_mlliteral_nfa
#undef update_mlliteral_type
}


//FIXME: NEED TO PROPERLY HANDLE EXPRESSIONS LIKE [a] or [ab] i.e non range expressions ... 
//       I think this is fixed know
//
//       NEED TO PROPERLY HANDLE NEGATION OF INDIVIUAL ELEMENTS LIKE IN THE CASE [^abc]
void
parse_bracket_expression(Parser * parser)
{
  // use this as the new bottom of the stack
  NFA * open_delim_p = new_literal_nfa(parser->lookahead.value, 0);
  push(parser->symbol_stack, open_delim_p);
  parser->scanner->parse_escp_seq = 0;
  parser_consume_token(parser);
printf("PARSE BRACKET EXPRESSION\n");
  
  // disable scanning escape sequences
  int negate_match = 0;
printf("\tTOKEN: %c\n", parser->lookahead.value);
  if(parser->lookahead.type == CIRCUMFLEX) {
    parser_consume_token(parser);
    negate_match = 1;
printf("NEGATE MATCH!\n");
  }

  NFA * range_nfa = new_range_nfa(negate_match);
  push(parser->symbol_stack, range_nfa);

  parse_matching_list(parser, range_nfa->parent, negate_match);

  if(parser->lookahead.type != CLOSEBRACKET) {
    fatal("Expected ]\n");
  }
  else {
    // resets charclass array
    token_in_charclass(0, 1); 
    parser_consume_token(parser);
    // re-enable scanning escape sequences
    parser->scanner->parse_escp_seq = 1;

    NFA * right = pop(parser->symbol_stack);

    if(right == open_delim_p) {
      fatal("Empty bracket expression\n");
    }
    
    NFA * left  = pop(parser->symbol_stack); //NULL;

    if(left != open_delim_p) {
      fatal("Error parsing bracket expression\n");
    }
/*
    while((left = pop(parser->symbol_stack)) != open_delim_p) {
      right = new_alternation_nfa(left, right);
    }
*/

// release open_delim_p
free_nfa(open_delim_p);

    push(parser->symbol_stack, right);
printf("PUSHED NFA BACK ONTO STACK: symbol stack top: 0x%x\n", peek(parser->symbol_stack));
    parse_quantifier_expression(parser);
  }

}


void
parse_literal_expression(Parser * parser)
{
  NFA * nfa;
  if(parser->lookahead.type == DOT) {
    //nfa = new_literal_nfa(DOT);
    nfa = new_literal_nfa(parser->lookahead.value, NFA_ANY);
  }
  else {
    //nfa = new_literal_nfa(parser->lookahead.value.name[0]);
    nfa = new_literal_nfa(parser->lookahead.value, 0);
  }
printf("parse_literal: '%c' (%u)\n\n", nfa->parent->value.literal, nfa->parent->value.literal);

  parser_consume_token(parser);

  push(parser->symbol_stack, nfa);

  parse_quantifier_expression(parser);

}


void
parse_paren_expression(Parser * parser)
{
  parser_consume_token(parser);

  // Used by PIPE to determine where lhs operand starts otherwise gets popped as
  // lhs operand in a concatenation wich will simply return the rhs operand.
  push(parser->symbol_stack, (void *)NULL);

  parse_regex(parser);

  if(parser->lookahead.type == CLOSEPAREN) {
    parser_consume_token(parser);
    parse_quantifier_expression(parser);
  }
  else {
    fatal("--Expected ')'\n");
  }

}


void
parse_sub_expression(Parser * parser)
{
  NFA * right;
  switch(parser->lookahead.type) {
    case DOT: // fallthrough
    case ALPHA: {
      parse_literal_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      NFA * left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case DOLLAR: {
      push(parser->symbol_stack, new_nfa(NFA_EOL_ANCHOR));
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      NFA * left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case CIRCUMFLEX: {
      push(parser->symbol_stack, new_nfa(NFA_BOL_ANCHOR));
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      NFA * left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case OPENPAREN: {
      parse_paren_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      NFA * left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case OPENBRACKET: {
printf("OPEN BRACKET!\n");
      parse_bracket_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      NFA * left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    default: {
    } break;
  }
}


void
parse_regex(Parser * parser)
{
  if(parser->lookahead.type == ALPHA
     || parser->lookahead.type == CIRCUMFLEX
     || parser->lookahead.type == DOLLAR
     || parser->lookahead.type == DOT
     || parser->lookahead.type == OPENBRACKET
     || parser->lookahead.type == OPENPAREN) {
    parse_sub_expression(parser);
  }
  else {
    //fatal("Expected char or '('\n");
    set_err_msg(parser, "NONFATAL: Expected char or '('\n");
  }
}


// SIMULATE THE NFA
#include "slist.h"

int
get_states(NFA * nfa, List * lp)
{
  int found_accepting_state = 0;
  if(nfa->value.type & ~(NFA_SPLIT|NFA_EPSILON)) {
    if(nfa->value.type == NFA_ACCEPTING) {
      found_accepting_state = 1;
printf("\t\tACCEPTING: nfa->value.type: %d vs. %d\n", nfa->value.type, NFA_ACCEPTING);
    }
    else if(nfa->value.type == NFA_BOL_ANCHOR) {
printf("\t\tBOL ANCHOR: nfa->value.type: %d vs. %d\n", nfa->value.type, NFA_ACCEPTING);
    }
    else if(nfa->value.type == NFA_EOL_ANCHOR) {
printf("\t\tEOL ANCHOR: nfa->value.type: %d vs. %d\n", nfa->value.type, NFA_ACCEPTING);
    }
    else if(nfa->value.type == NFA_RANGE) {
printf("\t\tAPPENDING: RANGE\n");
    }
    else if(nfa->value.type == NFA_NGLITERAL) {
printf("\t\tNEGATED LITREAL\n");
    }
    else {
printf("\t\tAPPENDING: '%c', type was: %d\n", nfa->value.literal, nfa->value.type);
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
  unsigned int mask = 0x001 << (c % 32 - 1);
  if(range[c/32 - 1] & mask) {
printf("LITERAL IN RANGE\n");
    return 1;
  }
printf("LITERAL NOT IN RANGE\n");
   return 0;
}


int
match(NFA * nfa, int c)
{
  int ret = 0;
  switch(nfa->value.type) {
    case NFA_ANY: {
printf("\tmatching DOT\n");
      ret = 1;
    } break;
    case NFA_RANGE: {
      ret = is_literal_in_range(*(nfa->value.range), c);
    } break;
    case NFA_NGLITERAL: {
//printf("\tmatch: '%c' != '%c'\n", nfa->value.literal, c);
      ret = !(c == nfa->value.literal);
    } break;
    default: {
//printf("\tmatch: '%c'(%lu) vs '%c'(%lu)\n", nfa->value.literal, nfa->value.literal, c, c);
      ret = (c == nfa->value.literal);
    } break;
  } 
  return ret;
}


typedef struct Match {
  char * buffer;
  int start;
  int end;
} Match;


void *
free_match_string(void * m)
{
printf("HERE1\n");
  if(m) {
    printf("FREE MATCH STRING 2\n");
    free(m);
  }
printf("HERE2\n");
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


#define EOL '\0'

typedef struct NFASim {
  enum {START, MATCHING} state;
  NFA * nfa;
  char * buffer;
  char * input_ptr;
  int match_start;
  List * state_set1;
  List * state_set2;
  List * matches;
} NFASim;


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
  while(sim->input_ptr[0] != EOL) {
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
      else if((match_end - match_start - 1) > 0) {
printf("Backtraking input pointer by : %d\n", match_end - match_start - 1);
        sim->input_ptr -= (match_end - match_start - 1);
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
printf("\n%d MATCHES FOUND IN INPUT: %s\n\n", match_list->size, ((Match *)(m->data))->buffer);
//printf("MATCH FOUND IN INPUT: %c\n", (((Match *)(m->data))->buffer)[0]);
  for(int i = 0; i < match_list->size; ++i, m = m->next) {
    int start = ((Match *)m->data)->start; 
    int end = ((Match *)m->data)->end;
    int match_len = end - start + 1;
printf("START: %d\n", start);
printf("END: %d\n", end);
printf("len: %d\n'", match_len);
    for(int j = 0; j < match_len; j++) {
printf("%c", (((Match *)(m->data))->buffer)[j+start]);
    }
printf("'\n\n");
  }
}


void
parser_free(Parser * parser)
{
  free_scanner(parser->scanner);
  stack_delete(parser->interval_stack, NULL);
  free_nfa(pop(parser->symbol_stack));
  stack_delete(parser->symbol_stack, NULL);
  free(parser);
}


void
free_nfa_sim(NFASim* nfa_sim)
{
printf("\nFREE SIM\n");
  list_free(nfa_sim->state_set1, NULL);
  list_free(nfa_sim->state_set2, NULL);
printf("FREE MATCH STRING\n");
  list_free(nfa_sim->matches, free_match_string);
  free(nfa_sim);
}


int
main(void)
{
  Parser * parser  = init_parser(stdin);
  NFASim * nfa_sim = NULL;
  printf("--> PHASE1: PARSING/COMPILING regex\n\n");
  parse_regex(parser);

  if(parser->err_msg_available) {
    printf("%s\n", parser->err_msg);
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
printf("Releasing parser and sim\n");
  parser_free(parser);
  if(nfa_sim) {
    free_nfa_sim(nfa_sim);
  }

  printf("\n");
  return 0;
}
