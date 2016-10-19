#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"
#include "errmsg.h"
#include "regex_parser.h"

#include <stdio.h>


void parse_paren_expression(Parser * parser);
void parse_literal_expression(Parser * parser);
void parse_quantifier_expression(Parser * parser);
void parse_sub_expression(Parser * parser);
void parse_bracket_expression(Parser * parser);


IntervalRecord *
new_interval_record(NFA * nfa, unsigned int min, unsigned int max)
{
  IntervalRecord * ir = xmalloc(sizeof * ir);
  ir->node = nfa;
  ir->min_rep = min;
  ir->max_rep = max;
  ir->count = 0;
  return ir;
}


static inline void
parser_consume_token(Parser * parser)
{
  parser->lookahead = *regex_scan(parser->scanner);
}

static inline void
parser_backtrack(Parser * parser, Token * lookahead_reset)
{
  unput(parser->scanner);
  unput(parser->scanner);
  parser_consume_token(parser);
//printf("\tLOOKAHEAD AFTER BACKTRACKING PARSER: %c\n", parser->lookahead.value);
  //parser->lookahead = *(lookahead_reset);
}


Parser *
init_parser(Scanner * scanner, ctrl_flags * cfl)
{
  if(!scanner) {
    fatal("FAILED TO INITIALIZE PARSER, NO SCANNER PROVIDED\n");
  }
  Parser * parser        = xmalloc(sizeof(*parser));
  parser->scanner        = scanner;
  parser->ctrl_flags     = cfl;
  parser->symbol_stack   = new_stack();
  parser->interval_stack = new_stack();
  parser->nfa_ctrl       = new_nfa_ctrl();
  parser_consume_token(parser);
//printf("HERE :)\n");
  return parser;
}


void
parse_quantifier_expression(Parser * parser)
{
#define DISCARD_WHITESPACE(p)                                             \
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

#define CHECK_MULTI_QUANTIFIER_ERROR                                      \
  if(quantifier_count >= 1)                                               \
    fatal(INVALID_MULTI_MULTI_ERROR)
  static int quantifier_count = 0;
  NFA * nfa;

  switch(parser->lookahead.type) {
    case PLUS: { 
//printf("POS CLOSURE\n");
      CHECK_MULTI_QUANTIFIER_ERROR;
      parser_consume_token(parser);
//printf("HERE3: LOOKAHEAD NOW %c\n", parser->lookahead.value);
      nfa = pop(parser->symbol_stack);
      push(parser->symbol_stack, new_posclosure_nfa(nfa));
      quantifier_count++;
      parse_quantifier_expression(parser);
      quantifier_count--;
    } break;
    case KLEENE: {
      CHECK_MULTI_QUANTIFIER_ERROR;
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      push(parser->symbol_stack, new_kleene_nfa(nfa));
      quantifier_count++;
      parse_quantifier_expression(parser);
      quantifier_count--;
    } break;
    case OPENBRACE: {
      // handle interval expression like in '<expression>{m,n}'
      int set_max = 0;
      int dec_pos = 0;
      unsigned int min = 0;
      unsigned int max = 0;
      IntervalRecord * interval_record;
      
      CHECK_MULTI_QUANTIFIER_ERROR;
      parser_consume_token(parser);
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

//printf("MIN: %d, MAX: %d\n", min, max);

      if(parser->lookahead.type == CLOSEBRACE) {
        if(set_max) {
          if((max != 0) && (min > max)) {
            fatal(INVALID_INTERVAL_EXPRESSION_ERROR);
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
            // if we hit this point then min == 0 and max == 0 which is
            // essentially a 'don't match' operation; so pop the nfa
            // off of the symbol_stack
            pop(parser->symbol_stack);
          }
        }
      }
      else {
        fatal("Syntax error at interval expression. Expected '}'\n");
      }
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
//printf("\tPIPE!!!\n\n");
      // reset the quantifier_count rhs of alternation
      quantifier_count = 0;
      parser_consume_token(parser);
      // concatenate everything on the stack unitl we see an open paren
      // or until nothing is left on the stack
      NFA * left = NULL;
      NFA *right = NULL;
      //while(parser->symbol_stack->size > 1) {
      while(parser->symbol_stack->size > 0) {
        right = pop(parser->symbol_stack);
        left  = pop(parser->symbol_stack);
        if(left == NULL) {
          // handle case like: (<expression>|<expression>)
//printf("LEFT PAREN POPPED: symbol stack top: 0x%x\n", parser->symbol_stack->head);
          push(parser->symbol_stack, right);
          break;
        }
        push(parser->symbol_stack, concatenate_nfa(left, right));
      }
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, (void *)NULL);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      push(parser->symbol_stack, new_alternation_nfa(left, right));
    } break;
    case CLOSEPAREN: // fallthrough
    default: {       // epsilon production
      break;
    }
  }
#undef CHECK_MULTI_QUANTIFIER_ERROR
#undef DISCARD_WHITESPACE
#undef CONVERT_TO_UINT
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
//printf("\%c(t0x%x) FOUND IN CHARCLASS AT idx: %d = 0x%x\n", element, element, i, charclass[i]);
      break;
    }
  }

  if(!found) {
//printf("\t\t%c(0x%x) INSERTED INTO CHARCLASS AT idx: %d = 0x%x\n", element, element, i, charclass[i]);
//#include <unistd.h>
//sleep(2);
    charclass[i] = element;
    used_charclass_slots++;
  }

  return found;
}


void
parse_matching_list(Parser * parser, NFA * range_nfa, int negate)
{
//printf("PARSE MATCHING LIST: parser->lookahead: %c\n", parser->lookahead.value);
  
  NFA * open_delim_p;
  char found = 0;
  unsigned int prev_token_val;
  static int matching_list_len = 0;
 
  Token prev_token = parser->lookahead;
//printf("\t0 TESTING: %s\n", parser->scanner->readhead);
  parser_consume_token(parser);
//printf("\t1 TESTING: %s\n", parser->scanner->readhead);
  Token lookahead = parser->lookahead;

  
  if(prev_token.type == __EOF) {
    return;
  }

//printf("0 - HERE: prev_token: %c, lookahead: %c\n", prev_token.value, lookahead.value);
  if(prev_token.type == OPENBRACKET  && (lookahead.type == DOT 
                                          ||  lookahead.type == COLON 
                                          ||  lookahead.type == EQUAL)) {
    symbol_type delim = lookahead.type;
    prev_token = parser->lookahead;

    //char * str_start = parser->scanner->readhead;
    char * str_start = get_scanner_readhead(parser->scanner);
    parser_consume_token(parser);

    // handle case where ']' immediately follows '['<delim> ... (e.x. '[.].]')
    if(parser->lookahead.type == CLOSEBRACKET) {
      //str_start = parser->scanner->readhead;
      str_start = get_scanner_readhead(parser->scanner);
      prev_token = parser->lookahead;
      push(parser->symbol_stack, new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, 0));
      parser_consume_token(parser);
    }


    while(parser->lookahead.type != __EOF) {
      if(parser->lookahead.type == CLOSEBRACKET && prev_token.type == delim) {
        break;
      }
      prev_token = parser->lookahead;
      parser_consume_token(parser);
    }
    

    if(parser->lookahead.type != CLOSEBRACKET && prev_token.type != delim) {
      fatal(MALFORMED_BRACKET_EXPRESSION_ERROR);
    }

    // notice that -1 removes the trailing delim
    //int coll_str_len = parser->scanner->readhead - str_start - 2;
    int coll_str_len = get_scanner_readhead(parser->scanner) - str_start - 2;

    if(coll_str_len <= 0) {
      return;
    }
//printf("\t3 TESTING: %s\n", str_start);
    char * collation_string = strndup(str_start, coll_str_len);
 
    // We've successfully parsed a collation class
    switch(delim) {
      case COLON: {
        // handle expression like '[[:alpha:]]'
//printf("COLON: HERE: prev_token: %c, lookahead: %c\n", prev_token.value, lookahead.value);
        if(update_range_w_collation(collation_string, coll_str_len, range_nfa, negate) == 0) {
          fatal(UNKNOWN_CHARCLASS_ERROR);
        }
      } break;
      case DOT: {
        warn("Collating-symbols not supported\n");
      } break;
      case EQUAL: {
        warn("Equivalence-calss expresions  not supported\n");
      } break;
      default: {
        // should never reach this point
      } break;
    }
    // discard the current ']' token
    parser_consume_token(parser);
    free(collation_string);
  }
  else if(prev_token.type == CLOSEBRACKET && matching_list_len > 0) {
//printf("--HERE: prev_token: %c, lookahead: %c\n", prev_token.value, lookahead.value);
    parser_backtrack(parser, &prev_token);
//printf("NOW: lookahead: %c\n", parser->lookahead.value);
    return;
  }
  else if(lookahead.type == __EOF) {
    // If we hit this point we're missing the closing ']'
    // handle error in parse_bracket_expression()
    return;
  }
  else {
//printf("HERE: prev_token: %c, lookahead: %c\n", prev_token.value, lookahead.value);
    if(lookahead.type == HYPHEN) {
      // process a range expression like [a-z], [a-Z], [A-Z], etc.
      int range_invalid = 1;
      parser_consume_token(parser); // consume hyphen
      lookahead = parser->lookahead;
      if(prev_token.value >= 'a' && prev_token.value <= 'z') {
        // special case [a-Z] where lowerboud 'a' has ascii value > upperbound 'Z'
        if(lookahead.value >= 'a' && lookahead.value <= 'z') {
          update_range_nfa(prev_token.value, lookahead.value, range_nfa, negate);
          range_invalid = 0;
        }
        else if(lookahead.value >= 'A' && lookahead.value <= 'Z') {
          update_range_nfa(prev_token.value, 'z', range_nfa, negate);
          update_range_nfa('A', lookahead.value, range_nfa, negate);
          range_invalid = 0;
        }
      }
      else if(prev_token.value <= lookahead.value) {
				// all other cases foce the lowerbound to have ascii value < upperbound
				if(prev_token.value >= 'A' && prev_token.value <= 'Z') {
          // special case where we want to restrain upperbound to ascii value <= 'Z'
					if(lookahead.value >= 'A' && lookahead.value <= 'Z') {
            update_range_nfa(prev_token.value, lookahead.value, range_nfa, negate);
						range_invalid = 0;
					}
				}
        else {
          update_range_nfa(prev_token.value, lookahead.value, range_nfa, negate);
					range_invalid = 0;
				}
      }

      if(range_invalid) {
        fatal(INVALID_RANGE_EXPRESSION_ERROR);
      }

      parser_consume_token(parser); // consume end bound
    }
    else {
      update_range_nfa(prev_token.value, prev_token.value, range_nfa, negate);
    }
  }
  ++matching_list_len;
  parse_matching_list(parser, range_nfa, negate);
  --matching_list_len;
}


//FIXME: NEED TO PROPERLY HANDLE EXPRESSIONS LIKE [a] or [ab] i.e non range expressions ... 
//       I think this is fixed know
//
//       NEED TO PROPERLY HANDLE NEGATION OF INDIVIUAL ELEMENTS LIKE IN THE CASE [^abc]
void
parse_bracket_expression(Parser * parser)
{
  // use this as the new bottom of the stack
  NFA * open_delim_p = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, 0);
  push(parser->symbol_stack, open_delim_p);
  //parser->scanner->parse_escp_seq = 0;
  CLEAR_ESCP_FLAG(parser->ctrl_flags);
  parser_consume_token(parser);
//printf("PARSE BRACKET EXPRESSION\n");
  
  // disable scanning escape sequences
  int negate_match = 0;
//printf("\tTOKEN: %c\n", parser->lookahead.value);
  if(parser->lookahead.type == CIRCUMFLEX) {
    parser_consume_token(parser);
    negate_match = 1;
//printf("NEGATE MATCH!\n");
  }

  NFA * range_nfa = new_range_nfa(parser->nfa_ctrl, negate_match);
  push(parser->symbol_stack, range_nfa);

  parse_matching_list(parser, range_nfa->parent, negate_match);

  if(parser->lookahead.type != CLOSEBRACKET) {
    fatal("Expected ]\n");
  }
  else {
    // resets charclass array
    token_in_charclass(0, 1); 
    
    // re-enable scanning escape sequences
   // parser->scanner->parse_escp_seq = 1;
    SET_ESCP_FLAG(parser->ctrl_flags);

    parser_consume_token(parser);
//printf("HERE2: LOOKAHEAD NOW %c\n", parser->lookahead.value);
    NFA * right = pop(parser->symbol_stack);

    if(right == open_delim_p) {
      fatal(EMPTY_BRACKET_EXPRESSION_ERROR);
    }
    
    NFA * left  = pop(parser->symbol_stack); //NULL;

    if(left != open_delim_p) {
      fatal("Error parsing bracket expression\n");
    }

// release open_delim_p
free_nfa(open_delim_p);

    push(parser->symbol_stack, right);
//printf("PUSHED NFA BACK ONTO STACK: symbol stack top: 0x%x\n", peek(parser->symbol_stack));
    parse_quantifier_expression(parser);
  }

//printf("done bracket expression: lookahead now: %c\n", parser->lookahead.value);
}


void
parse_literal_expression(Parser * parser)
{
  NFA * nfa;
  switch(parser->lookahead.type) {
    case DOT: {
      nfa = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, NFA_ANY);
    } break;
    case DOLLAR: {
      nfa = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, NFA_EOL_ANCHOR);
    } break;
    case CIRCUMFLEX: {
      nfa = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, NFA_BOL_ANCHOR);
    } break;
    default: {
      nfa = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, 0);
    }
  }
//printf("parse_literal: '%c' (%u)\n\n", nfa->parent->value.literal, nfa->parent->value.literal);

  parser_consume_token(parser);

//printf("lookahead now: %c\n", parser->lookahead.value);

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
//printf("MATCHING CLOSE PAREN\n");
    parser_consume_token(parser);
//printf("lookahead now: '%c'\n", parser->lookahead.value);
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
    case DOT:        // fallthrough
    case ALPHA:      // fallthrough
    case DOLLAR:     // fallthrough
    case CIRCUMFLEX: // fallthrough
    case ASCIIDIGIT: // fallthrough
    case CLOSEBRACKET: {
      parse_literal_expression(parser);
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
      parse_bracket_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      NFA * left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case __EOF: {
      //printf("__EOF\n");
      return;
    }
    default: {
//printf("ERROR? %c is this EOF? ==> %s\n", parser->lookahead.value, (parser->lookahead.value == EOF)? "YES": "NO");
    } break;
  }
}


void
parse_regex(Parser * parser)
{
  if(   parser->lookahead.type == ALPHA
     || parser->lookahead.type == ASCIIDIGIT
     || parser->lookahead.type == CLOSEBRACKET
     || parser->lookahead.type == CIRCUMFLEX
     || parser->lookahead.type == DOLLAR
     || parser->lookahead.type == DOT
     || parser->lookahead.type == OPENBRACKET
     || parser->lookahead.type == OPENPAREN) {
    parse_sub_expression(parser);
  }
  else {
    fatal("Expected char or '('\n");
    //set_err_msg(parser, "NONFATAL: Expected char or '('\n");
  }
}


void
parser_free(Parser * parser)
{
  free_scanner(parser->scanner);
  stack_delete(&(parser->interval_stack), NULL);
  free_nfa(pop(parser->symbol_stack));
  stack_delete(&(parser->symbol_stack), NULL);
  free(parser->nfa_ctrl);
  free(parser);
}
