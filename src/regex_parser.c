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


// If we are currently parsing a subexpression (i.e. '('<expression')'
// and we know there is a backreference to this capture group
// insert a marker node so the recognizer knows to start tracking
// what this matches
void
track_capture_group(Parser * parser, unsigned int type)
{
  int cg_idx = parser->cg_record.count - 1;
  if(parser->cg_record.count <= CAPTURE_GROUP_MAX) {
    if(type == NFA_CAPTUREGRP_BEGIN) {
      parser->cg_record.open_cgrp[parser->cg_record.next_cgrp_idx] = cg_idx;
      parser->cg_record.current_idx = parser->cg_record.next_cgrp_idx;
      ++(parser->cg_record.next_cgrp_idx);
      if(parser->cg_record.is_referenced[cg_idx] == 0) {
        // we keep count of how many capture groups we've seen
        // but only mark the capture group if it is referenced
        return;
      }
//printf("HERE OPEN: %d\n", cg_idx + 1);
    }
    else { // NFA_CAPTUREGRP_END
      --(parser->cg_record.next_cgrp_idx);
      parser->cg_record.current_idx = parser->cg_record.next_cgrp_idx;
      cg_idx = parser->cg_record.open_cgrp[parser->cg_record.next_cgrp_idx];
      if(parser->cg_record.is_referenced[cg_idx] == 0) {
        return;
      }
//printf("HERE CLOSE: %d\n", cg_idx + 1);
    }
    NFA * right = new_literal_nfa(parser->nfa_ctrl, 0, type);
    NFA * left  = pop(parser->symbol_stack);
    right->parent->id = cg_idx;
    push(parser->symbol_stack, concatenate_nfa(left, right));
  }
}


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
parser_backtrack(Parser * parser)
{
  unput(parser->scanner);
  unput(parser->scanner);
  parser_consume_token(parser);
}


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
  parser->interval_stack  = new_stack();
  parser->nfa_ctrl        = new_nfa_ctrl();
  parser_consume_token(parser);
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
      CHECK_MULTI_QUANTIFIER_ERROR;
      parser_consume_token(parser);
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
//printf("PIPE\n");
      // Let the parser know it is processing an alternation, this allows us
      // to properly handle backreferences
      ++(parser->in_alternation);
      parser->branch_id = parser->cg_record.count;
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
          push(parser->symbol_stack, right);
          break;
        }
        push(parser->symbol_stack, concatenate_nfa(left, right));
      }
      left = pop(parser->symbol_stack);
      // separation marker between alternation branches
      push(parser->symbol_stack, (void *)NULL);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      push(parser->symbol_stack, new_alternation_nfa(left, right));
//printf("PIPE DONE HERE\n");
      --(parser->in_alternation);
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
  NFA * open_delim_p = new_literal_nfa(parser->nfa_ctrl, parser->lookahead.value, 0);
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
    
    release_nfa(open_delim_p);

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
  regex_parser_start(parser);

  if(parser->paren_count > 0 && parser->ignore_missing_paren == 0) {
    if(parser->lookahead.type == CLOSEPAREN) {
      if(parser->paren_count == 0) fatal("Unmatched ')'\n");
      track_capture_group(parser, NFA_CAPTUREGRP_END);
      --(parser->paren_count);
      parser_consume_token(parser);
      parse_quantifier_expression(parser);
    }
    else {
      fatal("--Expected ')'\n");
    }
  }
  else {
    parse_quantifier_expression(parser);
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
          if(parser->cg_record.count > parser->branch_id) {
            --(parser->cg_record.count);
          }
        }
        else {
          // From here we return to parse_paren_expression which is expecting
          // a ')'. However, rather than making the parser backtrack we simply
          // tell the parser to ignore this requirement by setting the flag 
          // ignore_missing_paren. This allows the parser to continue trying to
          // parse the rest of input, if any.
          --(parser->paren_count);
          if(parser->cg_record.count > parser->branch_id) {
            --(parser->cg_record.count);
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
      parser->paren_count += 1;
      parser->cg_record.count += 1;
      track_capture_group(parser, NFA_CAPTUREGRP_BEGIN);
      parse_paren_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case OPENBRACKET: {
      parse_bracket_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case BACKREFERENCE: {
      // we start capture_group_count at - 1 and only increase from there
      // so we subtract 1 from the lookahead.value
      if(parser->lookahead.value == '0'
      || parser->lookahead.value > parser->cg_record.count
      || (parser->cg_record.next_cgrp_idx > 0 
         && parser->lookahead.value == parser->cg_record.open_cgrp[0] + 1)) {
        //printf("back reference val: %d -- %d\n", parser->lookahead.value, parser->cg_record.count);
        fatal("Invalid back-reference\n");
      }

      // If the backreference is in an alternation and reffers to a capture-group inside 
      // a separate branch that same alternation, then we should skip processing the backrefernce
      // since it could only match the empty string.
      int purge_branch = 0;
      if(parser->in_alternation) {
        // if next_cgrp_idx == 0 then there's currently no open cgrp
        if(parser->paren_count) {
          // at this point the back refernce correctly refers to a capture-group that
          // was defined prior to the backref itself, we now need to determine whether
          // that capture-group is on a different branch of the same alternation.
          // If it is we ignore this backreference
          //
          // open_cgrp[0] holds the id of the root of the current capture-group (or paren expression)
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
          else if(parser->lookahead.value > parser->cg_record.open_cgrp[0]
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
        left = new_backreference_nfa(parser->nfa_ctrl, parser->lookahead.value);
        push(parser->symbol_stack, left);
        parser_consume_token(parser);
        parse_quantifier_expression(parser);
        parse_sub_expression(parser);
      }
    } break;
    case CLOSEPAREN: // fallthrough
    case __EOF: return;
    default: {
      printf("ERROR? %c is this EOF? ==> %s\n",
             parser->lookahead.value,
             (parser->lookahead.value == EOF)? "YES": "NO");
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
  Scanner * new_scanner_state = init_scanner(
    parser->scanner->buffer,
    parser->scanner->buf_len,
    parser->scanner->line_len,
    parser->scanner->ctrl_flags
  );

  scanner_push_state(&(parser->scanner), new_scanner_state);

  char ** next = &(parser->scanner->readhead);
  if((*next)[0] != parser->scanner->eol_symbol) {
    char c = next_char(parser->scanner);
    int i = 0;
    while((*next)[0] != parser->scanner->eol_symbol) {
      if(c == '\\' && isdigit((*next)[0])) {
        parser->cg_record.is_referenced[(*next)[0] - '0' - 1] = 1;
      }
      c = next_char(parser->scanner);
      ++i;
    }
  }

  new_scanner_state->buffer = 0;
  new_scanner_state = scanner_pop_state(&(parser->scanner));
// should be released to a pool
  free_scanner(new_scanner_state);

  return;
}

void
parse_regex(Parser * parser)
{
  if(parser == 0) {
    fatal("No parser provided\n");
  }

  prescan_input(parser);
  regex_parser_start(parser);

// FIXME: This is a bit of a cluge...
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
//printf("LEAVING PARSER\n");
  // code for converting from an NFA to a DFA would be called here

  return;
}


void
parser_free(Parser * parser)
{
  //free_scanner(parser->scanner);
  stack_delete(&(parser->interval_stack), NULL);
  free_nfa(pop(parser->symbol_stack));
  stack_delete(&(parser->symbol_stack), NULL);
  free(parser->nfa_ctrl);
  free(parser);
}
