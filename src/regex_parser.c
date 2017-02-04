#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "misc.h"
#include "errmsg.h"
#include "regex_parser.h"

#define REGEX (parser->scanner->buffer)
#define READHEAD (parser->scanner->readhead)


void debug_print_collected(NFA *, NFA *);


static void parse_paren_expression(Parser * parser);
static void parse_literal_expression(Parser * parser);
static void parse_quantifier_expression(Parser * parser);
static void parse_sub_expression(Parser * parser);
static void parse_bracket_expression(Parser * parser);
static void regex_parser_start(Parser * parser);
static inline void parser_consume_token(Parser * parser);
static void __collect_adjacencies_helper(NFA *, NFA *);

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


// If we are currently parsing a subexpression (i.e. '('<expression')'
// and we know there is a backreference to this capture group
// insert a marker node so the recognizer knows to start tracking
// what this matches
int
track_capture_group(Parser * parser, unsigned int type)
{
  int ret = 0;
  if(cgrp_has_backref(parser->cgrp_map, parser->current_cgrp)) {
    ret = 1;
    NFA * right = new_literal_nfa(parser->nfa_ctrl, NFA_LITERAL, type);
    NFA * left  = pop(parser->symbol_stack);
    right->parent->id = parser->cgrp_count - 1;
    push(parser->symbol_stack, concatenate_nfa(left, right));
  }

  if(cgrp_is_complex(parser->cgrp_map, parser->current_cgrp)) {
    ret = 2;
    if(type == NFA_CAPTUREGRP_BEGIN) {
      ++(parser->next_interval_id);
    }
  }

  if(type == NFA_CAPTUREGRP_END) {
    if(parser->root_cgrp == parser->current_cgrp) {
      parser->root_cgrp = 0;
    }
    --(parser->current_cgrp);
  }
  return ret;
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
    int dec_pos = 0;                                                      \
    while((p)->lookahead.type == ASCIIDIGIT) {                            \
      dec_pos *= 10;                                                      \
      (interval) = ((interval) * dec_pos) + ((p)->lookahead.value - '0'); \
      dec_pos++;                                                          \
      parser_consume_token((p));                                          \
    }                                                                     \
  } while(0)


  // handle interval expression like in '<expression>{m,n}'
  int set_max = 0;
  int min = -1;
  int max = -1;
  NFA * interval_nfa;
  NFA * target;
  target = pop(parser->symbol_stack);

  int braces_balance = 0;

  // Loop over interval that are back to back like in:
  // <expression>{min,Max}{min,Max}...{min,Max}
  // NOTE: this does not handle cases like:
  //   (<expression>{min,Max}){min,Max}...{min,Max}
  //   i.e: we break out of the loop when we hit anything other than a '{'
  //        after seeing a '}'.
  do {
    int local_min = -1;
    int local_max = -1;
    braces_balance = 0;
    parser_consume_token(parser);
    DISCARD_WHITESPACE(parser);
    CONVERT_TO_UINT(parser, local_min);
    DISCARD_WHITESPACE(parser);

    // Note we use lookahead.value instead of lookahead.type
    // since the scanner never assigns special meaning to ','
    if(parser->lookahead.value == ',') {
      parser_consume_token(parser);
      DISCARD_WHITESPACE(parser);
      CONVERT_TO_UINT(parser, local_max);
      DISCARD_WHITESPACE(parser);
    }

    min = (min == -1) ? local_min : (local_min >= 0) ? min * local_min : min;
    max = (max == -1) ? local_max : (local_max >= 0) ? max * local_max : max;

    set_max = (max > 0) ? 1 : 0;
    if(parser->lookahead.type == CLOSEBRACE) {
      braces_balance = 1;
      parser_consume_token(parser);
    }

    if(min == -1) {
      // we can't combine the two intervals in cases like <expression>{,Max}{min,Max}
      // since the first interval needs to be able to match between 0 and Max times.
      // Combining the two would force us to match at least 'min' times which is not
      // the intention.
      break;
    }
  } while(braces_balance && (parser->lookahead.type == OPENBRACE));

  if(braces_balance) {
    if(set_max) {
      if((max != 0) && (min > max)) {
        fatal(INVALID_INTERVAL_EXPRESSION_ERROR);
      }
      if(min == 0 && max == 1) {
        // <expression>{0,1} ; equivalent to <expression>?
        push(parser->symbol_stack, new_qmark_nfa(target));
      }
      else if(min == 1 && max == 1) {
        // return unmodified target
        push(parser->symbol_stack, target);
      }
      else {
        if(min > 0 && max == 0) {
          // <expression>{Min,} ;match at least Min, at most Infinity
          interval_nfa = new_interval_nfa(target, min, max);
          ++(parser->interval_count);
        }
        else {
          // <expression>{,Max} ;match between 0 and Max
          // <expression>{Min,Max} ;match between Min and Max
          min = (min >= 0) ? min : 0;
          interval_nfa = new_interval_nfa(target, min, max);
          ++(parser->interval_count);
        }
        push(parser->symbol_stack, interval_nfa);
      }
    }
    else {
      if(min == 1 && max == -1) {
        // <expression>{1,} ; equivalent to <expression>+
        push(parser->symbol_stack, new_posclosure_nfa(target));
      }
      else if(min > 0 && max == -1) {
        // <expression>{M}
        interval_nfa = new_interval_nfa(target, min, max);
        ++(parser->interval_count);

        push(parser->symbol_stack,
          concatenate_nfa(pop(parser->symbol_stack), interval_nfa));
      }
      else if(min == -1 && max == -1) {
        // <expression>{,} ;create a kleenes closure
        push(parser->symbol_stack, new_kleene_nfa(target));
      }
      else {
        // if we hit this point then min == 0 and max == 0 which is
        // essentially a 'don't match' operation; so pop the nfa
        // off of the symbol_stack
        release_nfa(target);
        if(parser->in_alternation) {
          --(parser->subtree_branch_count);
        }
        if((list_size(parser->symbol_stack) == 0) && (parser->in_alternation == 0)) {
          NFA * accept = new_nfa(parser->nfa_ctrl, NFA_ACCEPTING);
          accept->parent = accept;
          push(parser->symbol_stack, accept);
        }
        goto DONT_COUNT_LOOP;
      }
    }
  }
  else {
    //parser_fatal("Syntax error at interval expression. Expected '}'", REGEX, READHEAD, 0);
    parser_fatal(MISSING_CLOSE_BRACE, REGEX, READHEAD, 0);
  }
  ++(parser->loops_to_track);
DONT_COUNT_LOOP:
  return;
#undef DISCARD_WHITESPACE
#undef CONVERT_TO_UINT
}


void
parse_quantifier_expression(Parser * parser)
{
  NFA * nfa;

  switch(parser->lookahead.type) {
    case PLUS: {
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      push(parser->loop_nfas, nfa);
      nfa = new_posclosure_nfa(nfa);
      push(parser->symbol_stack, nfa);
      ++(parser->loops_to_track);
      parse_quantifier_expression(parser);
    } break;
    case KLEENE: {
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      push(parser->loop_nfas, nfa);
      nfa = new_kleene_nfa(nfa);
      push(parser->symbol_stack, nfa);
      ++(parser->loops_to_track);
      parse_quantifier_expression(parser);
    } break;
    case QMARK: {
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      nfa = new_qmark_nfa(nfa);
      push(parser->symbol_stack, nfa);
      ++(parser->loops_to_track);
      parse_quantifier_expression(parser);
    } break;
    case OPENBRACE: {
      parse_interval_expression(parser);
      parse_quantifier_expression(parser);
    } break;
    case PIPE: {
      parser->total_branch_count += (parser->in_alternation != 0) ? 1 : 2;
      ++(parser->in_alternation);
      parser->branch_id = parser->current_cgrp - 1;
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
        parser->subtree_branch_count = 2;
        parser->in_new_cgrp = 0;
      }
      else {
        parser->subtree_branch_count += 1;
      }

      // separation marker between alternation branches
      push(parser->symbol_stack, (void *)NULL);
      parse_sub_expression(parser);

      if(peek(parser->symbol_stack) != 0) {
        NFA * right = pop(parser->symbol_stack);
        NFA * left  = pop(parser->symbol_stack);
        while(left != NULL) {
          right = concatenate_nfa(left, right);
          left  = pop(parser->symbol_stack);
        }
        push(parser->symbol_stack, left);
        push(parser->branch_stack, right);
      }

      --(parser->in_alternation);

      if(parser->in_alternation == 0) {
        unsigned int sz = list_size(parser->branch_stack);
        left = new_alternation_nfa(parser->nfa_ctrl, parser->branch_stack, sz, NULL);
        parser->subtree_branch_count = 0;
        push(parser->symbol_stack, left);
      }
      else {
        left = new_alternation_nfa(parser->nfa_ctrl, parser->branch_stack,
          parser->subtree_branch_count, NULL);
        push(parser->symbol_stack, left);
        parser->subtree_branch_count = 0;
      }
    } break;
    case __EOF: // fallthrough
    default: {
      // epsilon production
      break;
    }
  }
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
      break;
    }
  }

  if(found == 0) {
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
        parser->lookahead.value, NFA_LITERAL));

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
    char * collation_string = strndup(str_start, coll_str_len);

    // We've successfully parsed a collation class
    switch(delim) {
      case COLON: {
        // handle expression like '[[:alpha:]]'
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
    parser_backtrack(parser);
    return;
  }
  else if(lookahead.type == __EOF) {
    // If we hit this point we're missing the closing ']'
    // handle error in parse_bracket_expression()
    return;
  }
  else {
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
  // disable scanning escape sequences
  CLEAR_ESCP_FLAG(&CTRL_FLAGS(parser));
  parser_consume_token(parser);

  int negate_match = 0;
  if(parser->lookahead.type == CIRCUMFLEX) {
    parser_consume_token(parser);
    negate_match = 1;
  }

  NFA * range_nfa = new_range_nfa(parser->nfa_ctrl, INTERVAL(parser), negate_match, parser->branch_id);
  push(parser->symbol_stack, range_nfa);

  parse_matching_list(parser, range_nfa->parent, negate_match);

  if(parser->lookahead.type != CLOSEBRACKET) {
    //fatal("Expected ]\n");
    parser_fatal(MISSING_CLOSE_BRACKET, REGEX, READHEAD, 0);
  }
  else {
    // resets charclass array
    token_in_charclass(0, 1);

    // re-enable scanning escape sequences
    SET_ESCP_FLAG(parser->ctrl_flags);

    parser_consume_token(parser);
    NFA * right = pop(parser->symbol_stack);

    if(right == open_delim_p) {
      fatal(EMPTY_BRACKET_EXPRESSION_ERROR);
    }

    NFA * left  = pop(parser->symbol_stack); //NULL;

    if(left != open_delim_p) {
      fatal("Error parsing bracket expression\n");
    }

    push(parser->symbol_stack, right);
    parse_quantifier_expression(parser);
  }
}


void
parse_literal_expression(Parser * parser)
{
  NFA * nfa;
  switch(parser->lookahead.type) {
    case DOT: {
      nfa = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, NFA_ANY);
      parser_consume_token(parser);
    } break;
    case DOLLAR: {
      nfa = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, NFA_EOL_ANCHOR);
      parser_consume_token(parser);
    } break;
    case CIRCUMFLEX: {
      nfa = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, NFA_BOL_ANCHOR);
      parser_consume_token(parser);
    } break;
    default: {
       int stop = 0;
       unsigned int len = 2;
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

       if(len > 1) {
         nfa = new_lliteral_nfa(parser->nfa_ctrl, src, len);
       }
       else {
         nfa = new_literal_nfa(parser->nfa_ctrl, c, NFA_LITERAL);
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
  ++(parser->paren_count);
  parser->current_cgrp = ++(parser->cgrp_count);

  if(parser->root_cgrp == 0) {
    parser->root_cgrp = parser->current_cgrp;
  }

  track_capture_group(parser, NFA_CAPTUREGRP_BEGIN);
  subtree_branch_count = parser->subtree_branch_count;
  parser->subtree_branch_count = 0;
  return subtree_branch_count;
}


void
update_close_paren_accounting(Parser * parser, unsigned int subtree_br_cnt)
{
  parser->subtree_branch_count = subtree_br_cnt;
}


void
parse_paren_expression(Parser * parser)
{
  unsigned int subtree_branch_count = update_open_paren_accounting(parser);
  int in_new_cgrp = parser->in_new_cgrp;
  int preceding_stack_size = list_size(parser->symbol_stack);

  parser_consume_token(parser);

  // Used by PIPE to determine where lhs operand starts otherwise gets popped
  // as lhs operand in a concatenation wich will simply return the rhs
  // operand.
  push(parser->symbol_stack, (void*)NULL);
  regex_parser_start(parser);

  if(parser->lookahead.type == CLOSEPAREN) {
    if(parser->paren_count == 0) {
      //fatal("Unmatched ')'\n");
      parser_fatal(MISSING_CLOSE_PAREN, REGEX, READHEAD, 0);
    }
    int has_backref = track_capture_group(parser, NFA_CAPTUREGRP_END);
    --(parser->paren_count);
    parser_consume_token(parser);

    update_close_paren_accounting(parser, subtree_branch_count);

    parser->in_new_cgrp = in_new_cgrp;;

    NFA * right = pop(parser->symbol_stack);
    NFA * left = NULL;
    unsigned int sz = list_size(parser->symbol_stack);
    while((sz > preceding_stack_size)) {
      left =  pop(parser->symbol_stack);
      right = concatenate_nfa(left, right);
      --sz;
    }

    if(has_backref) {
      right = concatenate_nfa(pop(parser->symbol_stack), right);
    }

    push(parser->symbol_stack, right);
    parse_quantifier_expression(parser);
  }
  else {
    parser_fatal(MISSING_CLOSE_PAREN, REGEX, READHEAD, 0);
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
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case OPENPAREN: {
      parse_paren_expression(parser);
      parse_sub_expression(parser);
    } break;
    case OPENBRACKET: {
      parse_bracket_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case BACKREFERENCE: {
      if(parser->lookahead.value == 0
      || (parser->lookahead.value > parser->cgrp_count)
      || (parser->current_cgrp > 0
         && parser->lookahead.value == parser->root_cgrp)) {
        parser_fatal(INVALID_BACKREF, REGEX, (READHEAD), -1);
      }
      left = new_backreference_nfa(parser->nfa_ctrl, INTERVAL(parser),
        parser->lookahead.value, parser->branch_id);
      push(parser->symbol_stack, left);
      parser_consume_token(parser);
      parse_quantifier_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case CLOSEBRACE: {
      parser_fatal(MISSING_OPEN_BRACE, REGEX, READHEAD, 0);
    } break;
    case CLOSEPAREN: // fallthrough
      if(parser->paren_count == 0) {
        parser_fatal(MISSING_CLOSE_PAREN, REGEX, READHEAD, 0);
        //fatal("Unmatched ')'\n");
      }
    default: {
      // epsilon production
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
    fatal(INVALID_PARSE_START);
  }
}


// Pre-scan the input regex looking for '\<n>' where <n> is the
// number of the capture-group referenced by the backreference
static int
prescan_input(Parser * parser)
{
  int line_len = parser->scanner->line_len;

  if(line_len > MAX_REGEX_LENGTH || line_len <=1) {
    return 0;
  }

  // FIXME don't alway use alloc... have the scanner keep a large enough buffer
  // to hold several scanner's buffers ?
  char * tmp_buffer = malloc(parser->scanner->buf_len);
  memcpy(tmp_buffer, parser->scanner->buffer, parser->scanner->buf_len);


//  if(line_len > 0) {
    Scanner * new_scanner_state = init_scanner(
      get_filename(parser->scanner), // TEST
      tmp_buffer,
      parser->scanner->buf_len,
      parser->scanner->line_len,
      parser->scanner->ctrl_flags
    );

    scanner_push_state(&(parser->scanner), new_scanner_state);

    char ** next = &(parser->scanner->readhead);
    int eol = parser->scanner->eol_symbol;

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
          default: {
            c = next_char(parser->scanner);
          } break;
        }
      }
    }
    new_scanner_state = scanner_pop_state(&(parser->scanner));
    free_scanner(new_scanner_state);
//  }

  return 1;
}


int
insert_progress_nfa(List * loop_nfas)
{
  int sz = list_size(loop_nfas);
  if(sz < 1 ) {
    return 0;
  }
  int needs_progress = 0;
  int count = 0;
  int stop = 0;
  NFA * looper = list_shift(loop_nfas);
  NFA * walker = NULL;
  for(int i = 0; i < sz; ++i) {
    if(looper && (looper->value.type & NFA_SPLIT)) {
      walker = looper->out1;
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
          case NFA_INTERVAL:
          case NFA_CAPTUREGRP_END:
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
        ++count;
      }
    }
    looper = list_shift(loop_nfas);
  }
  return count;
}


static void *
compare(void * a, void * b)
{
  if(a == b) {
    return a;
  }
  return NULL;
}


static void
__collect_adjacencies_helper(NFA * current, NFA * visiting)
{
//if(current->value.type == NFA_INTERVAL) {
//  printf("0 - current: [0x%x]:%d -- visiting: [0x%x]:%d\n",
//  current, current->value.type, visiting, visiting->value.type);
//}
  static int recursion = 0;
  if(visiting->value.type == NFA_EPSILON) {
    visiting->visited = 1;
    ++recursion;
    __collect_adjacencies_helper(current, visiting->out2);
    --recursion;
    visiting->visited = 0;
  }
  else {
    // visiting is not an EPSILON
    if(visiting->visited) {
      // we've hit this node before
      switch(visiting->value.type) {
        case NFA_TREE:  // fallthrough
        case NFA_SPLIT:
        case (NFA_SPLIT|NFA_PROGRESS): break;
        default: {
          // if the node is matchable and is not already part of
          // our adjacency list.. add it.
          if(list_search(&(current->reachable), visiting, compare) == NULL) {
            list_append(&(current->reachable), visiting);
          }
        }
      }
    }
    else {
      // we haven't seen this node before
      switch(visiting->value.type) {
        case NFA_TREE: {
          visiting->visited = 1;
          NFA * branch = NULL;
          for(int i = 0; i < list_size(visiting->value.branches); ++i) {
            branch = list_get_at(visiting->value.branches, i);
            ++recursion;
            __collect_adjacencies_helper(current, branch);
            --recursion;
          }
          visiting->visited = 0;
        } break;
        case (NFA_SPLIT|NFA_PROGRESS):
        case NFA_SPLIT: {
          if(current->value.type == NFA_INTERVAL && (visiting->value.literal == '+')) {
            if(list_search(&(current->reachable), visiting, compare) == NULL) {
//printf("YAY: {%d,%d} --> [0x%x]\n", current->value.min_rep, current->value.max_rep, visiting);
              list_append(&(current->reachable), visiting);
            }
          }
          else {
            visiting->visited = 1;
            ++recursion;
            __collect_adjacencies_helper(current, visiting->out1);
            __collect_adjacencies_helper(current, visiting->out2);
            --recursion;
            visiting->visited = 0;
          }
        } break;
        default: {
          // don't skip over NFA_INTERVALS if we haven't seen them yet
          if(current == visiting) {
            if(recursion == 0) {
              ++recursion;
              __collect_adjacencies_helper(current, visiting->out2);
              --recursion;
              break;
            }
          }
          if(list_search(&(current->reachable), visiting, compare) == NULL) {
            list_append(&(current->reachable), visiting);
          }
        }
      }
    }
  }
  if(visiting->value.type == NFA_ACCEPTING) {
//printf("current[0x%x] %d -- reaches accept\n", current, current->value.type);
    current->reaches_accept = 1;
  }

}


void
collect_adjacencies(NFA * start, int total_collectables)
{
  if(start == NULL) {
    return;
  }

  NFA * current = start;
  NFA * visiting = start;

  // FIXME: resuse parser->branch_stack as a list
  List * l = new_list();

  __collect_adjacencies_helper(current, visiting);
  list_append(l, current);
  for(int i = 0; i < list_size(l); ++i) {
    if(i > total_collectables) {
printf("i: %d/%d\n", i, total_collectables);
      break; // should never hit this condition
    }
    current = list_get_at(l, i);
    for(int j = 0; j < list_size(&(current->reachable)); ++j) {
      visiting = list_get_at(&(current->reachable),j);
      if(list_search(l, visiting, compare)) {
        continue;
      }
      if(visiting->done == 0 && current != visiting && visiting->value.type != NFA_ACCEPTING) {
        visiting->visited = 1;
        if(visiting->value.type == NFA_INTERVAL) {
          __collect_adjacencies_helper(visiting, visiting->out1);
          visiting->value.split_idx = list_size(&(visiting->reachable));
// OMG WHAT A FUCKING KLUGE!
          ListItem * old_head = visiting->reachable.head;
          ListItem * old_tail = visiting->reachable.tail;
          visiting->reachable.size = 0;
          visiting->reachable.head = visiting->reachable.tail = NULL;
// END KLUGE
          __collect_adjacencies_helper(visiting, visiting->out2);
// OMG WHAT A FUCKING KLUGE!
          old_tail->next = visiting->reachable.head;
          visiting->reachable.size += visiting->value.split_idx;
          visiting->reachable.head = old_head;
// OMG WHAT A FUCKING KLUGE!
          visiting->done = 1;
//debug_print_collected(current, NULL);
        }
        else if(visiting->value.type == NFA_SPLIT){
          __collect_adjacencies_helper(visiting, visiting->out1);
          __collect_adjacencies_helper(visiting, visiting->out2);
//debug_print_collected(visiting, visiting->out2);
        }
        else {
          __collect_adjacencies_helper(visiting, visiting->out2);
        }
        visiting->visited = 0;
        if(list_search(l, visiting, compare) == NULL) {
          list_append(l, visiting);
        }
      }
    }
  }
//printf("\n");
  list_free(l, NULL);
}


int
parse_regex(Parser * parser)
{
  if(parser == 0) {
    fatal("No parser provided\n");
  }

  int ret = 0;

  if(prescan_input(parser)) {
    regex_parser_start(parser);

    if(parser->symbol_stack->size > 1) {
      NFA * right = pop(parser->symbol_stack);
      NFA * left  = pop(parser->symbol_stack);
      right = concatenate_nfa(left, right);
      while(parser->symbol_stack->size > 0) {
        left = pop(parser->symbol_stack);
        right = concatenate_nfa(left, right);
      }
      push(parser->symbol_stack, right);
    }

    // give the last accepting state an id
    mark_nfa(peek(parser->symbol_stack));

    parser->loops_to_track += insert_progress_nfa(parser->loop_nfas);

// TEST FIXME -- define an interface for this in nfa(.c/.h)
    //parser->total_nfa_ids = ((NFA *)peek(parser->symbol_stack))->ctrl->next_seq_id - 1;
    parser->total_nfa_ids = ((NFA *)peek(parser->symbol_stack))->id;
    if((((NFA *)peek(parser->symbol_stack))->parent)->value.type  & ~(NFA_SPLIT|NFA_EPSILON)) {
      NFA * start_node = new_nfa(parser->nfa_ctrl, NFA_EPSILON);
      NFA * right = pop(parser->symbol_stack);
      start_node->out1 = start_node->out2 = right->parent;
      right->parent = start_node;
      push(parser->symbol_stack, right);
    }
    collect_adjacencies((((NFA *)peek(parser->symbol_stack))->parent),
      (parser->total_nfa_ids + parser->interval_count));
//printf("HERE\n");
//exit(1);
// END TEST
    ret = 1;
  }

  return ret;
}


void
parser_free(Parser * parser)
{
  free_nfa(((NFA *)peek(parser->symbol_stack)));
  stack_delete((parser->symbol_stack), NULL);
  stack_delete((parser->branch_stack), NULL);
  list_free((parser->loop_nfas), NULL);
  free(parser->nfa_ctrl);
  free(parser);
}










void
debug_print_collected(NFA * current, NFA * visiting)
{
  if(current->value.type == NFA_INTERVAL) {
    printf("\ninterval: {%d, %d}\n", current->value.min_rep, current->value.max_rep);
    printf(" left list: %d\n", list_size(&(current->reachable)));
      //for(int i = 0; i < list_size(&(current->reachable)); ++i) {
      for(int i = 0; i < current->value.split_idx; ++i) {
        if(i == 0) {
          printf("\t[0x%x]:%d:%d",
          ((NFA *)list_get_at(&(current->reachable), i)),
          i, ((NFA *)list_get_at(&(current->reachable), i))->value.type);
          switch(((NFA *)list_get_at(&(current->reachable), i))->value.type) {
            case NFA_INTERVAL: {
              printf(":{%d, %d}",
                ((NFA *)list_get_at(&(current->reachable), i))->value.min_rep,
                ((NFA *)list_get_at(&(current->reachable), i))->value.max_rep);
            } break;
            default: {
              printf(":%c",
                ((NFA *)list_get_at(&(current->reachable), i))->value.literal
                );
            }
          }
        }
        else {
          printf(" -- [0x%x]:%d:%d",
          ((NFA *)list_get_at(&(current->reachable), i)),
          i, ((NFA *)list_get_at(&(current->reachable), i))->value.type);
          switch(((NFA *)list_get_at(&(current->reachable), i))->value.type) {
            case NFA_INTERVAL: {
              printf(":{%d, %d}",
                ((NFA *)list_get_at(&(current->reachable), i))->value.min_rep,
                ((NFA *)list_get_at(&(current->reachable), i))->value.max_rep);
            } break;
            default: {
              printf(":%c",
                ((NFA *)list_get_at(&(current->reachable), i))->value.literal
              );
            }
          }
        }
      }
      printf("\n");
      printf("right list: %d\n", list_size(&(current->reachable)) - current->value.split_idx);
        for(int i = 0; current->value.split_idx + 1 < list_size(&(current->reachable)); ++i) {
          current = ((NFA *)list_get_at(&(current->reachable), i));
          if(i == 0) {
            printf("\t[0x%x]:%d:%d",
            ((NFA *)list_get_at(&(current->reachable), i)),
            i, ((NFA *)list_get_at(&(current->reachable), i))->value.type);
            switch(((NFA *)list_get_at(&(current->reachable), i))->value.type) {
              case NFA_INTERVAL: {
                printf(":{%d, %d}",
                  ((NFA *)list_get_at(&(current->reachable), i))->value.min_rep,
                  ((NFA *)list_get_at(&(current->reachable), i))->value.max_rep);
              } break;
              default: {
                printf(":%c",
                  ((NFA *)list_get_at(&(current->reachable), i))->value.literal
                  );
              }
            }
          }
          else {
            printf(" -- [0x%x]:%d:%d",
            ((NFA *)list_get_at(&(current->reachable), i)),
            i, ((NFA *)list_get_at(&(current->reachable), i))->value.type);
            switch(((NFA *)list_get_at(&(current->reachable), i))->value.type) {
              case NFA_INTERVAL: {
                printf(":{%d, %d}",
                  ((NFA *)list_get_at(&(current->reachable), i))->value.min_rep,
                  ((NFA *)list_get_at(&(current->reachable), i))->value.max_rep);
              } break;
              default: {
                printf(":%c",
                  ((NFA *)list_get_at(&(current->reachable), i))->value.literal
                );
              }
            }
          }
        }
        printf("\n\n");

  }
  else {
    printf("[0x%x]:%d:%c\n", current, current->value.type, current->value.literal);
    for(int i = 0; i < list_size(&(current->reachable)); ++i) {
      if(i == 0) {
        printf("\t%d:%d", i, ((NFA *)list_get_at(&(current->reachable), i))->value.type);
        switch(((NFA *)list_get_at(&(current->reachable), i))->value.type) {
          case NFA_INTERVAL: {
            printf(":{%d, %d}",
              ((NFA *)list_get_at(&(current->reachable), i))->value.min_rep,
              ((NFA *)list_get_at(&(current->reachable), i))->value.max_rep);
          } break;
          default: {
            printf(":%c", ((NFA *)list_get_at(&(current->reachable), i))->value.literal);
          }
        }
      }
      else {
        printf(" -- %d:%d", i, ((NFA *)list_get_at(&(current->reachable), i))->value.type);
        switch(((NFA *)list_get_at(&(current->reachable), i))->value.type) {
          case NFA_INTERVAL: {
            printf(":{%d, %d}",
              ((NFA *)list_get_at(&(current->reachable), i))->value.min_rep,
              ((NFA *)list_get_at(&(current->reachable), i))->value.max_rep);
          } break;
          default: {
            printf(":%c", ((NFA *)list_get_at(&(current->reachable), i))->value.literal);
          }
        }
      }
    }
    printf("\n");
  }
}
