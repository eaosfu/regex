#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "misc.h"
#include "errmsg.h"
#include "regex_parser.h"

#include <stdio.h>

static void parse_paren_expression(Parser * parser);
static void parse_literal_expression(Parser * parser);
static void parse_quantifier_expression(Parser * parser);
static void parse_sub_expression(Parser * parser);
static void parse_bracket_expression(Parser * parser);
static void regex_parser_start(Parser * parser);
static inline void parser_consume_token(Parser * parser);



// NOTE:
// set capture_group_count to -1 to avoid having to subtrack by 1 every time
// we need to update the the capture group list;
Parser *
init_parser(Scanner * scanner, ctrl_flags * cfl)
{
  if(!scanner) {
    fatal("FAILED TO INITIALIZE PARSER, NO SCANNER PROVIDED\n");
  }
  Parser * parser         = xmalloc(sizeof(*parser));
  parser->scanner         = scanner;
  parser->ctrl_flags      = cfl;
  parser->symbol_stack    = new_stack();
  parser->branch_stack    = new_stack();
  parser->loop_nfas       = new_list();
  parser->nfa_ctrl        = new_nfa_ctrl();
  parser_consume_token(parser);
  return parser;
}


NFA *
tie_branches(Parser * parser, NFA * nfa, unsigned int increment)
{
  if(parser->tie_branches) {
    nfa = nfa_tie_branches(nfa, parser->branch_stack, parser->tie_branches);
    parser->tie_branches = 0;
    parser->subtree_branch_count = 1;
    parser->push_to_branch_stack += increment;
  }
  return nfa;
}


void
tie_and_push_branches(Parser * parser, NFA * nfa, unsigned int increment)
{
  if(parser->tie_branches) {
    nfa = nfa_tie_branches(nfa, parser->branch_stack, parser->tie_branches);
    parser->tie_branches = 0;
    parser->subtree_branch_count = 1;
    parser->push_to_branch_stack += increment;
    push(parser->symbol_stack, nfa);
  }
}


// If we are currently parsing a subexpression (i.e. '('<expression')'
// and we know there is a backreference to this capture group
// insert a marker node so the recognizer knows to start tracking
// what this matches
void
track_capture_group(Parser * parser, unsigned int type)
{
  if(cgrp_has_backref(parser->cgrp_map, parser->current_cgrp)) {
    NFA * right = new_literal_nfa(parser->nfa_ctrl, NULL, NFA_LITERAL, type,
                                  parser->branch_id);
    NFA * left  = pop(parser->symbol_stack);
    right->parent->id = parser->cgrp_count - 1;
    push(parser->symbol_stack, concatenate_nfa(left, right));
  }

  if(cgrp_is_complex(parser->cgrp_map, parser->current_cgrp)) {
    if(type == NFA_CAPTUREGRP_BEGIN) {
      ++(parser->in_complex_cgrp);
      ++(parser->is_complex);
//printf("Entering complex cgrp %d -- interval is: %d -- next interval: %d\n",
//  parser->current_cgrp, parser->influencing_interval, parser->next_interval_id);
      parser->influencing_interval = parser->next_interval_id;
      ++(parser->next_interval_id);
    }
    else {
      --(parser->influencing_interval);
      --(parser->in_complex_cgrp);
      //parser->is_complex = (parser->in_complex_cgrp != 0);
    }
  }

  if(type == NFA_CAPTUREGRP_END) {
    if(parser->root_cgrp == parser->current_cgrp) {
      parser->root_cgrp = 0;
    }
    parser->current_cgrp = --(parser->current_cgrp);
  }
//  else {
//  if(parser->in_complex_cgrp)
//printf("in complex cgrp: %d -- influenced by interval %d \n",
//  parser->current_cgrp, parser->influencing_interval);
//  }

}


static inline void
parser_consume_token(Parser * parser)
{
  parser->lookahead = *regex_scan(parser->scanner);
}


static inline void
parser_backtrack(Parser * parser)
{
  unput(parser->scanner);
  unput(parser->scanner);
  parser_consume_token(parser);
}


void
parse_interval_expression(Parser * parser)
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


  // handle interval expression like in '<expression>{m,n}'
  int set_max = 0;
  int dec_pos = 0;
  unsigned int min = 0;
  unsigned int max = 0;
  NFA * interval_nfa;
  NFA * target;

  if(parser->tie_branches) {
    target = tie_branches(parser, pop(parser->symbol_stack), 0);
  }
  else {
    target = pop(parser->symbol_stack);
  }

  int influences_cgrp = ((get_cur_pos(parser->scanner) - 1)[0] == ')');
  int braces_balance = 0;
  
  --(parser->is_complex);

//  CHECK_MULTI_QUANTIFIER_ERROR;

  // Loop over interval that are back to back like in:
  // <expression>{min,Max}{min',Max'}...{min'',Max''}
  do {
    int local_min = 0;
    int local_max = 0;
    braces_balance = 0;
    parser_consume_token(parser);
    DISCARD_WHITESPACE(parser);
    CONVERT_TO_UINT(parser, local_min);
    DISCARD_WHITESPACE(parser);

    // Note we use lookahead.value instead of lookahead.type
    // since the scanner never assigns special meaning to ','
    if(parser->lookahead.value == ',') {
      dec_pos = 0;
      parser_consume_token(parser);
      DISCARD_WHITESPACE(parser);
      CONVERT_TO_UINT(parser, local_max);
      DISCARD_WHITESPACE(parser);
      set_max = 1;
    }

    min = (min == 0) ? local_min : (local_min) ? min * local_min : min;
    max = (max == 0) ? local_max : (local_max) ? max * local_max : max;

    if(parser->lookahead.type == CLOSEBRACE) {
      braces_balance = 1;
      parser_consume_token(parser);
    }
  } while(parser->lookahead.type == OPENBRACE);

  if(braces_balance) {
    if(set_max) {
      if((max != 0) && (min > max)) {
        fatal(INVALID_INTERVAL_EXPRESSION_ERROR);
      }
      if(min == 0 && max == 0) {
        // <expression>{,} ;create a kleenes closure
        //push(parser->symbol_stack, new_kleene_nfa(pop(parser->symbol_stack)));
        push(parser->symbol_stack, new_kleene_nfa(target));
      }
      else if(min == 0 && max == 1) {
        // <expression>{0,1} ; equivalent to <expression>?
        //push(parser->symbol_stack, new_qmark_nfa(pop(parser->symbol_stack)));
        push(parser->symbol_stack, new_qmark_nfa(target));
      }
      else if(min == 1 && max == 1) {
        // return unmodified target
        push(parser->symbol_stack, target);
      }
      else if(min == 1 && max == 0) {
        // <expression>{1,} ; equivalent to <expression>+
        //push(parser->symbol_stack, new_posclosure_nfa(pop(parser->symbol_stack)));
        push(parser->symbol_stack, new_posclosure_nfa(target));
      }
      else {
        if(min > 0 && max == 0) {
          // <expression>{Min,} ;match at least Min, at most Infinity
// FIXME kleene is not the right one to use here!... INSERT INTERVAL NODE
//       HANDLE PROCESSING IN THE RECOGNIZER.
//push(parser->symbol_stack, new_kleene_nfa(pop(parser->symbol_stack)));
          /*
           * interval_nfa = pop(parser->symbol_stack);
           * push(parser->symbol_stack, new_interval_nfa(interval_nfa, min, max));
           */
//printf("populating interval: %d\n", parser->next_interval_id);
          interval_nfa = new_interval_nfa(parser->nfa_ctrl, target,
            &((parser->interval_list)[parser->next_interval_id]), min, max);
        }
        else {
          // <expression>{,Max} ;match between 0 and Max
          // <expression>{Min,Max} ;match between Min and Max
          /* interval_nfa = pop(parser->symbol_stack);
           * push(parser->symbol_stack, new_interval_nfa(interval_nfa, min, max));
           */
//printf("-- populating interval: %d, parent interval: %d\n", parser->next_interval_id - 1, 
//(parser->is_complex) ? parser->next_interval_id : -1);

          if(influences_cgrp) {
            // handle case like '(<expression>){min,MAX}'
            interval_nfa = fill_interval_nfa(parser->nfa_ctrl, target,
              PROVISIONED_INTERVAL(parser), PARENT_INTERVAL(parser), min, max);
          }
          else {
            // handle case like 'a{min,MAX}'
            interval_nfa = new_interval_nfa(parser->nfa_ctrl, target, INTERVAL(parser), min, max);
          }
        }
        push(parser->symbol_stack, interval_nfa);
      }
    }
    else {
      if(min > 0) {
        // <expression>{M}
        /*
         * interval_nfa = pop(parser->symbol_stack);
         * push(parser->symbol_stack, new_interval_nfa(interval_nfa, min, max));
         */
//printf("populating interval: %d\n", parser->next_interval_id);
          interval_nfa = new_interval_nfa(parser->nfa_ctrl, target,
            &((parser->interval_list)[parser->next_interval_id]), min, max);

          push(parser->symbol_stack,
            concatenate_nfa(pop(parser->symbol_stack), interval_nfa));
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
#undef DISCARD_WHITESPACE
#undef CONVERT_TO_UINT
}


void
parse_quantifier_expression(Parser * parser)
{
#define CHECK_MULTI_QUANTIFIER_ERROR                                      \
  if(quantifier_count >= 1)                                               \
    fatal(INVALID_MULTI_MULTI_ERROR)

  static int quantifier_count = 0;
  static unsigned int last_interval_min = 0;
  static unsigned int last_interval_max = 0;
  NFA * nfa;

  switch(parser->lookahead.type) {
    case PLUS: { 
      CHECK_MULTI_QUANTIFIER_ERROR;
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
push(parser->loop_nfas, nfa);
      nfa = new_posclosure_nfa(nfa);
      nfa = tie_branches(parser, nfa, 0);
      push(parser->symbol_stack, nfa);
      quantifier_count++;
      parse_quantifier_expression(parser);
      quantifier_count--;
    } break;
    case KLEENE: {
      CHECK_MULTI_QUANTIFIER_ERROR;
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
push(parser->loop_nfas, nfa);
      nfa = new_kleene_nfa(nfa);
      nfa = tie_branches(parser, nfa, 0);
      push(parser->symbol_stack, nfa);
      quantifier_count++;
      parse_quantifier_expression(parser);
      quantifier_count--;
    } break;
    case QMARK: {
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      nfa = new_qmark_nfa(nfa);
      nfa = tie_branches(parser, nfa, 0);
      push(parser->symbol_stack, nfa);
      parse_quantifier_expression(parser);
    } break;
    case OPENBRACE: {
      parse_interval_expression(parser);
      quantifier_count++;
      parse_quantifier_expression(parser);
      quantifier_count--;
    } break;
    case PIPE: {
      // In case of conditions like (<expression>|<expression>)|<expression>
      if(parser->tie_branches) {
        parser->tie_branches = 0;
      }
      // Let the parser know it is processing an alternation, this allows us
      // to properly handle backreferences
      ++(parser->in_alternation);
      parser->branch_id = parser->cgrp_count;
      // reset the quantifier_count for rhs of alternation
      quantifier_count = 0;
      parser_consume_token(parser);
      // concatenate everything on the stack unitl we see an open paren
      // or until nothing is left on the stack
      NFA * left = NULL;
      NFA *right = NULL;
      while(parser->symbol_stack->size > 0) {
        right = pop(parser->symbol_stack);
        left  = pop(parser->symbol_stack);
        if(left == NULL) {
          // handle case like: (<expression>|<expression>)
          push(parser->symbol_stack, (void *)NULL);
          push(parser->symbol_stack, right);
          break;
        }
        push(parser->symbol_stack, concatenate_nfa(left, right));
      }

      if((left = pop(parser->symbol_stack)) != 0) {
        push(parser->branch_stack, left);
      }
if(parser->in_new_cgrp) {
  parser->subtree_branch_count += (parser->subtree_branch_count == 0) ? 2: 1;
  parser->in_new_cgrp = 0;
}
//printf("\thit pipe\n", parser->subtree_branch_count);

      // separation marker between alternation branches
      push(parser->symbol_stack, (void *)NULL);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      //push(parser->symbol_stack, new_alternation_nfa(left, right));


      int close_capture_group = 0;
      if(right != 0) {
//printf("pushing right: '%c' --- %c%s\n",
//  right->parent->value.literal, parser->lookahead.value, parser->scanner->readhead);
        if(right->parent->value.type == NFA_CAPTUREGRP_BEGIN) {
          close_capture_group = 1;
 //printf("NFA CAPTURE GROUP BEGIN POPPED\n");
        }
        else {
          push(parser->branch_stack, right);
        }
      }

      //printf("PIPE DONE HERE\n");
      --(parser->in_alternation);

      if(parser->in_alternation == 0) {
        unsigned int sz = list_size(parser->branch_stack);
//printf("TOTAL SUBTREE BRANCHES: %d -- subtree branches %d\n",
//  list_size(parser->branch_stack), parser->subtree_branch_count);
        left = new_alternation_nfa(parser->nfa_ctrl, parser->branch_stack, sz, NULL);
        parser->subtree_branch_count = 0;
        parser->tie_branches = 0;
        if(close_capture_group) {
          push(parser->symbol_stack, concatenate_nfa(right, left));
        }
        else {
          push(parser->symbol_stack, left);
        }
      }


    } break;
    case CLOSEPAREN: // fallthrough
    default: {       // epsilon production
      break;
    }
  }
#undef CHECK_MULTI_QUANTIFIER_ERROR
}


int
token_in_charclass(unsigned int element, int reset)
{
  int found = 0;
  int i = 0;
  static int used_charclass_slots = 0;
  static unsigned int charclass[128] = {0};

  if(reset) {
    for(; i < 128; ++i) {
      charclass[i] = 0;
    }
    used_charclass_slots = 0;
    return 0;
  }

  i = 0;
  for(; i < used_charclass_slots; ++i) {
    if(element == charclass[i]) {
      found = 1;
//printf("\%c(t0x%x) FOUND IN CHARCLASS AT idx: %d = 0x%x\n", element, element, i, charclass[i]);
      break;
    }
  }

  if(found == 0) {
//printf("\t\t%c(0x%x) INSERTED INTO CHARCLASS AT idx: %d = 0x%x\n", element, element, i, charclass[i]);
    charclass[i] = element;
    used_charclass_slots++;
  }

  return found;
}


void
parse_matching_list(Parser * parser, NFA * range_nfa, int negate)
{
#define DOT_COLON_OR_EQUAL(l) \
  ((l).type == DOT  || (l).type == COLON || (l).type == EQUAL)

  static int matching_list_len = 0;

  Token prev_token = parser->lookahead;
  parser_consume_token(parser);
  Token lookahead = parser->lookahead;

  if(prev_token.type == __EOF) {
    return;
  }

  if(prev_token.type == OPENBRACKET && DOT_COLON_OR_EQUAL(lookahead)) {
    symbol_type delim = lookahead.type;
    prev_token = parser->lookahead;

    char * str_start = get_scanner_readhead(parser->scanner);
    parser_consume_token(parser);

    // handle case where ']' immediately follows '['<delim> ... (e.x. '[.].]')
    if(parser->lookahead.type == CLOSEBRACKET) {
      str_start = get_scanner_readhead(parser->scanner);
      prev_token = parser->lookahead;

      push(parser->symbol_stack, new_literal_nfa(parser->nfa_ctrl, 
        INTERVAL(parser), parser->lookahead.value, NFA_LITERAL, parser->branch_id));

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
    parser_backtrack(parser);
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

#undef DOT_COLON_OR_EQUAL
  return;
}


void
parse_bracket_expression(Parser * parser)
{
  // use this as the new bottom of the stack
  void * open_delim_p = (void *)NULL;
  push(parser->symbol_stack, open_delim_p);
  CLEAR_ESCP_FLAG(&CTRL_FLAGS(parser));
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

  NFA * range_nfa = new_range_nfa(parser->nfa_ctrl, INTERVAL(parser), negate_match, parser->branch_id);
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
    
//    release_nfa(open_delim_p);
//free_nfa(open_delim_p);

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
      nfa = new_literal_nfa(parser->nfa_ctrl, INTERVAL(parser), 
        parser->lookahead.value, NFA_ANY, parser->branch_id);
      parser_consume_token(parser);
    } break;
    case DOLLAR: {
      nfa = new_literal_nfa(parser->nfa_ctrl, INTERVAL(parser), 
        parser->lookahead.value, NFA_EOL_ANCHOR, parser->branch_id);
      parser_consume_token(parser);
    } break;
    case CIRCUMFLEX: {
      nfa = new_literal_nfa(parser->nfa_ctrl, INTERVAL(parser),
        parser->lookahead.value, NFA_BOL_ANCHOR, parser->branch_id);
      parser_consume_token(parser);
    } break;
    default: {
       int stop = 0;
       unsigned int len = 2;
       int followed_by_interval = 0;
       char * src = get_cur_pos(parser->scanner);
       CLEAR_ESCP_FLAG(&CTRL_FLAGS(parser));

       parser_consume_token(parser);
       Token next = parser->lookahead;

       while(stop == 0) {
         switch(next.type) {
           case ASCIIDIGIT:    // fall-through
           case ALPHA: {
             if(next.value == '\\') {
               parser_backtrack(parser);
               stop = 1;
               --len;
               continue;
             }
           } break;
           case OPENBRACE:
           case KLEENE:        // fall-through
           case QMARK:         // fall-through
           case PLUS: {
             --len;
             parser_backtrack(parser);
             if(len > 1) {
               --len;
               parser_backtrack(parser);
             }
             stop = 1;
             continue;
           } break;
           default: {
             --len;
             parser_backtrack(parser);
             stop = 1;
             continue;
           } break;
         }
         parser_consume_token(parser);
         next = parser->lookahead;
         ++len;
       }

       char c = parser->lookahead.value;

       SET_ESCP_FLAG(&CTRL_FLAGS(parser));

       NFA * interval = NULL;
       if((followed_by_interval == 0)
       && (cgrp_is_complex(parser->cgrp_map, parser->current_cgrp))) {
         interval = INTERVAL(parser);
       }
       else {
         //interval = parse_interval_expression();
       }

       if(len > 1) {
         nfa = new_lliteral_nfa(parser->nfa_ctrl, interval, src, len,
           parser->branch_id);
       }
       else {
         nfa = new_literal_nfa(parser->nfa_ctrl, interval, c, NFA_LITERAL,
           parser->branch_id);
       }
       parser_consume_token(parser);
    }
  }
  push(parser->symbol_stack, nfa);
  parse_quantifier_expression(parser);
}


unsigned int
update_open_paren_accounting(Parser * parser)
{
  unsigned int subtree_branch_count;
  parser->in_new_cgrp = 1;
  ++(parser->paren_count);// += 1;
  parser->current_cgrp = ++(parser->cgrp_count);

  if(parser->root_cgrp == 0) {
    parser->root_cgrp = parser->current_cgrp;
  }

  track_capture_group(parser, NFA_CAPTUREGRP_BEGIN);


  if(parser->tie_branches) {
    tie_and_push_branches(parser, pop(parser->symbol_stack), 
      ((parser->push_to_branch_stack == 0) ? 2 : 1));
  }

  subtree_branch_count = parser->subtree_branch_count;
  parser->subtree_branch_count = 0;
  return subtree_branch_count;
}


void
update_close_paren_accounting(Parser * parser, unsigned int subtree_br_cnt)
{
  if(parser->subtree_branch_count > 0) {
    parser->tie_branches = parser->subtree_branch_count;
  }
  parser->subtree_branch_count = subtree_br_cnt;
}


void
parse_paren_expression(Parser * parser)
{
  unsigned int subtree_branch_count = update_open_paren_accounting(parser);
  parser_consume_token(parser);

  // Used by PIPE to determine where lhs operand starts otherwise gets popped
  // as lhs operand in a concatenation wich will simply return the rhs 
  // operand.
  push(parser->symbol_stack, (void *)NULL);
  regex_parser_start(parser);

  if(parser->paren_count > 0 && parser->ignore_missing_paren == 0) {
    if(parser->lookahead.type == CLOSEPAREN) {
      if(parser->paren_count == 0) {
        fatal("Unmatched ')'\n");
      }
      track_capture_group(parser, NFA_CAPTUREGRP_END);
      --(parser->paren_count);
      parser_consume_token(parser);
      parser->in_new_cgrp = 0;

      parse_quantifier_expression(parser);
      update_close_paren_accounting(parser, subtree_branch_count);
    }
    else {
      fatal("--Expected ')'\n");
    }
  }
  else {
    parse_quantifier_expression(parser);
    update_close_paren_accounting(parser, subtree_branch_count);
  }
  // don't ignore another missing paren unless explicitly told to do so again.
  parser->ignore_missing_paren = 0;
  return;
}


// NOTE: we need to free these nodes as they're being popped off the stack
// This function is only called after we've fully processed the first branch
// of an alternation and we know that the rest of the enclosing expression
// will not be able to match.
static inline void
parser_purge_branch(Parser * parser)
{
//printf("PURGING BRANCH\n");
  NFA * popped = NULL;
  while((popped = pop(parser->symbol_stack)) != NULL) release_nfa(popped);
  int stop = 0;
  while(stop == 0) {
    parser_consume_token(parser);
//printf("SKIPPING OVER: %c\n", parser->lookahead.value);
    switch(parser->lookahead.type) {
      case PIPE: {
        push(parser->symbol_stack, (void *)NULL);
        parser_consume_token(parser);
        parse_sub_expression(parser);
//printf("HERE: LOOKAHEAD: %c!\n", parser->lookahead.value);
        stop = 1;
      } break;
      case CLOSEPAREN: { // THINK ABOUT THIS A BIT MORE!!!
        if(parser->paren_count > 1) {
          --(parser->paren_count);
          if(parser->cgrp_count > parser->branch_id) {
            --(parser->cgrp_count);
          }
        }
        else {
          // From here we return to parse_paren_expression which is expecting
          // a ')'. However, rather than making the parser backtrack we simply
          // tell the parser to ignore this requirement by setting the flag 
          // ignore_missing_paren. This allows the parser to continue trying to
          // parse the rest of input, if any.
          --(parser->paren_count);
          if(parser->cgrp_count > parser->branch_id) {
            --(parser->cgrp_count);
          }
          parser_consume_token(parser);
          parser->ignore_missing_paren = 1;
          stop = 1;
        }
      } break;
      case __EOF: {
        if(parser->paren_count) {
          fatal("Expected ')'\n");
        }
        stop = 1;
      } break;
    }
  }

  // if there are still open parens then we should ignore the missing paren
  if(parser->paren_count) {
    parser->ignore_missing_paren = 1;
  }

  return;
}


void
parse_sub_expression(Parser * parser)
{
  NFA * right = NULL;
  NFA * left  = NULL;
  switch(parser->lookahead.type) {
    case DOT:        // fallthrough
    case COLON:      // fallthrough
    case ALPHA:      // fallthrough
    case DOLLAR:     // fallthrough
    case CIRCUMFLEX: // fallthrough
    case ASCIIDIGIT: {
      parse_literal_expression(parser);

      if(parser->tie_branches) {
        tie_and_push_branches(parser, pop(parser->symbol_stack), 1);
      }

      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case OPENPAREN: {
      parse_paren_expression(parser);
      parse_sub_expression(parser);

      parser->push_to_branch_stack -= (parser->push_to_branch_stack > 1) ? 1 : 0;
      if(parser->push_to_branch_stack == 1) {
        // if we're here then we know we were the first to start this trend
        // so we can end it
        parser->push_to_branch_stack = 0;
        push(parser->branch_stack, pop(parser->symbol_stack));
      }
      else {
// FIXME: we might not have anything on the stack!!!
        right = pop(parser->symbol_stack);
        left = pop(parser->symbol_stack);
        push(parser->symbol_stack, concatenate_nfa(left, right));
      }

    } break;
    case OPENBRACKET: {
      parse_bracket_expression(parser);

      if(parser->tie_branches) {
        tie_and_push_branches(parser, pop(parser->symbol_stack), 1);
      }

      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case BACKREFERENCE: {
      // we start capture_group_count at - 1 and only increase from there
      // so we subtract 1 from the lookahead.value
      if(parser->lookahead.value == '0'
      || parser->lookahead.value > parser->cgrp_count
      || (parser->current_cgrp > 0 
         && parser->lookahead.value == parser->root_cgrp)) {
        fatal("Invalid back-reference\n");
      }
      // If the backreference is in an alternation and reffers to a capture-group inside 
      // a separate branch that same alternation, then we should skip processing the backrefernce
      // since it could only match the empty string.
      int purge_branch = 0;
      if(parser->in_alternation) {
        if(parser->paren_count) {
          // At this point the back refernce correctly refers to a capture-group that
          // was defined prior to the backref itself, we now need to determine whether
          // that capture-group is on a different branch of the same alternation.
          // If it is we ignore this backreference
          //
          // root_cgrp holds the id of the root of the current capture-group (or paren expression)
          // subtree:
          //
          //   i.e. <expression>-->(<--(...(...<expression>...)...)...)
          //   the arrows indicate the root
          //
          // The capture-group referenced by the current backref must be strickly less than the
          // id of the current capture-group root. Otherwise it can only match the empty string, and
          // hence should be ignored.
          if(parser->lookahead.value == parser->branch_id) {
            purge_branch = 1;
          }
          else if(parser->lookahead.value > (parser->root_cgrp - 1)
          && parser->lookahead.value < parser->branch_id) {
            purge_branch = 1;
          }
        }
        else {
          if(parser->lookahead.value <= parser->branch_id) {
            purge_branch = 1;
          }
        }
      }
      if(purge_branch) {
        parser_purge_branch(parser);
      }
      else {
        left = new_backreference_nfa(parser->nfa_ctrl, INTERVAL(parser),
          parser->lookahead.value, parser->branch_id);

        left = tie_branches(parser, left, 1);

        push(parser->symbol_stack, left);
        parser_consume_token(parser);
        parse_quantifier_expression(parser);
        parse_sub_expression(parser);
      }
    } break;
    case CLOSEPAREN: // fallthrough
    case __EOF: {
      if(list_size(parser->symbol_stack) == 0) {
        // we either didn't parse anything
        // or the entire expression is an alternation
        parser->tie_branches = 0;
      }
      return;
    }
    default: {
//printf("ERROR? %c is this EOF? ==> %s\n",
//  parser->lookahead.value,
//  (parser->lookahead.value == EOF)? "YES": "NO");
    } break;
  }
  return;
}


void
regex_parser_start(Parser * parser)
{
  if(parser->lookahead.type == ALPHA
  || parser->lookahead.type == ASCIIDIGIT
  || parser->lookahead.type == BACKREFERENCE
  || parser->lookahead.type == CIRCUMFLEX
  || parser->lookahead.type == DOLLAR
  || parser->lookahead.type == DOT
  || parser->lookahead.type == OPENBRACKET
  || parser->lookahead.type == OPENPAREN) {
    parse_sub_expression(parser);
  }
  else {
    fatal("Expected char or '('\n");
  }
}


// Pre scan the input regex looking for '\<n>' where <n> is the
// number of the capture-group referenced by the backreference
static void
prescan_input(Parser * parser)
{
  if(parser->scanner->line_len < MINIMUM_COMPLEX_REGEX_LENGTH) {
    return;
  }

  Scanner * new_scanner_state = init_scanner(
    parser->scanner->buffer,
    parser->scanner->buf_len,
    parser->scanner->line_len,
    parser->scanner->ctrl_flags
  );

  scanner_push_state(&(parser->scanner), new_scanner_state);

  char ** next = &(parser->scanner->readhead);
  int eol = parser->scanner->eol_symbol;

  // FIXME: implement function to get scanner input length
  unsigned int regex_length = parser->scanner->line_len - 2;

///printf("regex length: %d\n", regex_length);



  // A Complex-Interval is one that contains loops within the scope of the
  // interval.
  //
  // Max number of possible interval expressions is length of (regex - 1)/3
  // since simplest valid 'complex-interval' expression is: <expression>{,}
  // for each 'complex-interval' expression store the capture-group it
  // influences in the lower char and the index
  char * complex_intervals = xmalloc(((regex_length - 1)/3) * 2);
  int next_complex_interval = 0;


  // FIXME: we might want more than 9... 
  char barckrefs[CAPTURE_GROUP_MAX] = {0};
  int next_backref = 0;
  
  
  unsigned int paren_count = 0;
  unsigned int cgrp_count = 0;
  unsigned int current_cgrp = 0;
  unsigned int complex_interval_count = 0;

  if((*next)[0] != eol) {
    char c = next_char(parser->scanner);
    while(c != eol) {
      switch(c) {
        case '\\': {
          c = next_char(parser->scanner);
          if(isdigit(c)) {
            mark_closure_map_backref(parser->cgrp_map, c - '0');
          }
          c = next_char(parser->scanner);
          continue;
        } break;
        case '(': {
          ++paren_count;
          ++cgrp_count;
          current_cgrp = cgrp_count;
          c = next_char(parser->scanner);
        } break;
        case ')': {
          --paren_count;
          c = next_char(parser->scanner);
          if(c == '{') {
            mark_closure_map_complex(parser->cgrp_map, current_cgrp);
            c = next_char(parser->scanner);
            ++complex_interval_count;
            // record which interval affects the current cgrp
          }
          --current_cgrp;
          continue;
        } break;
        default: {
          c = next_char(parser->scanner);
        } break;
      }
    }
  }
//printf("There are %d complex intervals\n", complex_interval_count);
//printf("There are %d intervals\n", interval_count);
  parser->interval_list_sz = complex_interval_count;
  if(complex_interval_count) parser->requires_backtracking = 1;
  parser->interval_list = malloc(sizeof(*(parser->interval_list)) * complex_interval_count);
  new_scanner_state->buffer = 0;
  new_scanner_state = scanner_pop_state(&(parser->scanner));
// should be released to a pool
  free_scanner(new_scanner_state);

  return;
}


void
insert_progress_nfa(List * loop_nfas)
{
  int sz = list_size(loop_nfas);
  if(sz < 1 ) {
    return;
  }
  int needs_progress = 0;
  int stop = 0;
  NFA * looper = list_shift(loop_nfas);
  NFA * walker = looper->out1;
  for(int i = 0; i < sz; ++i) {
    stop = needs_progress = 0;
    while(stop == 0) {
      switch(walker->value.type) {
        case NFA_SPLIT: {
          if(walker == looper) {
            needs_progress = 1;
            stop = 1;
            continue;
          }
          switch(walker->value.literal) {
            case '*': // fallthrough
            case '+': {
              if(walker->out2 == looper->out2) {
                walker = walker->out1;
              }
              else {
                walker = walker->out2;
              }
            } break;
            case '?': walker = walker->out1; break;
          }
        } break;
        case NFA_EPSILON: {
          walker = walker->out2;
        } break;
        default: {
          stop = 1;
        } break;
      }
    }
    if(needs_progress) {
      NFA_TRACK_PROGRESS(looper);
      // re-use list
      list_append(loop_nfas, (void *)&(looper->last_match));
    }
  }
}


void
parse_regex(Parser * parser)
{
  if(parser == 0) {
    fatal("No parser provided\n");
  }

  prescan_input(parser);
  regex_parser_start(parser);

//fatal("DONE PARSING\n");

// FIXME: This is a bit of a cluge...
  if(parser->symbol_stack->size > 1) {
    NFA * right = pop(parser->symbol_stack);
    NFA * left  = pop(parser->symbol_stack);
    right = concatenate_nfa(left, right);
    while(parser->symbol_stack->size > 0) {
      left = pop(parser->symbol_stack);
      right = concatenate_nfa(left, right);
//printf("STUCK?\n");
    }
    push(parser->symbol_stack, right);
  }

  // FIXME: should only run this if the recognizer will be backtracking
  insert_progress_nfa(parser->loop_nfas);

//printf("LEAVING PARSER\n");
  // code for converting from an NFA to a DFA would be called here

  return;
}


static inline void
parser_free_interval_list(Parser * parser)
{
  for(int i = (parser->interval_list_sz - 1); i >= 0; --i) {
    free((void *)&((parser->interval_list)[i]));
  }
}


void
parser_free(Parser * parser)
{
/*FIXME: 
  free_nfa(pop(parser->symbol_stack));
*/
  stack_delete(&(parser->symbol_stack), NULL);
  stack_delete(&(parser->branch_stack), NULL);
  free(parser->nfa_ctrl);
  free(parser);
}
