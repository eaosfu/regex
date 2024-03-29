#include "misc.h"
#include "errmsg.h"
#include "compile.h"
#include "regex_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGEX (parser->scanner->buffer)
#define READHEAD (parser->scanner->readhead)
#define PROGRAM_NAME (parser->program_name)


static void parse_paren_expression(Parser * parser);
static void parse_literal_expression(Parser * parser);
static void parse_quantifier_expression(Parser * parser);
static void parse_sub_expression(Parser * parser);
static void parse_bracket_expression(Parser * parser);
static void regex_parser_start(Parser * parser);
static inline void parser_consume_token(Parser * parser);

Parser *
init_parser(const char * program_name, Scanner * scanner, ctrl_flags * cfl)
{
  if(!scanner) {
    fatal("FAILED TO INITIALIZE PARSER, NO SCANNER PROVIDED\n");
  }
  Parser * parser         = xmalloc(sizeof(*parser));
  parser->scanner         = scanner;
  parser->ctrl_flags      = cfl;
  parser->symbol_stack    = new_stack();
  parser->branch_stack    = new_stack();
  parser->synth_patterns  = new_list();
  parser->nfa_ctrl        = new_nfa_ctrl();
  parser->program_name    = program_name;

  parser_consume_token(parser);

  if(CTRL_FLAGS(parser) & IGNORE_CASE_FLAG) {
    char * c = get_cur_pos(parser->scanner);
    while(c != get_buffer_end(parser->scanner) - 1){
      *c = toupper(*c);
      ++c;
    }
  }

  return parser;
}


// If we are currently parsing a subexpression (i.e. '('<expression')')
// and we know there is a backreference to this capture group
// insert a marker node so the recognizer knows to start tracking
// what this matches
static int
track_capture_group(Parser * parser, unsigned int type)
{
  int ret = 0;
  if(cgrp_has_backref(parser->cgrp_map, parser->current_cgrp)) {
    ret = 1;
    NFA * right = new_literal_nfa(parser->nfa_ctrl, NFA_LITERAL, type);
    NFA * left  = pop(parser->symbol_stack);

    if(type == NFA_CAPTUREGRP_BEGIN) {
      parser->capgrp_record[parser->current_cgrp - 1].next_id =
        right->parent->id;
    }

    // set the 'proper' id for the NFA_CAPTUREGRP_* node
    right->parent->id = (parser->paren_stack)[parser->paren_idx - 1] - 1;
    push(parser->symbol_stack, concatenate_nfa(left, right));
  }

  if(type == NFA_CAPTUREGRP_END) {
    if(ret) {
      parser->capgrp_record[parser->current_cgrp - 1].end =
        get_cur_pos(parser->scanner);
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


static int
merge_intervals(Parser * parser, int min, int max, NFA * target)
{
  int ret = 0;
  NFA * tmp = target->parent;
  while((tmp->value.type == NFA_EPSILON) && (tmp = tmp->out2));
  if(parser->prev_interval_head == tmp && parser->prev_interval->out2->out2 == NULL) {
    if(min == 0 || parser->prev_interval->value.min_rep == 0) {
      parser->prev_interval->value.min_rep += min;
    }
    else {
      parser->prev_interval->value.min_rep *= min;
    }

    if(max == 0 || parser->prev_interval->value.max_rep == 0) {
      parser->prev_interval->value.max_rep += max;
    }
    else {
      parser->prev_interval->value.max_rep *= max;
    }
    push(parser->symbol_stack, target);
    ret = 1;
  }
  return ret;
}


static void
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
        parser_fatal(PROGRAM_NAME, INVALID_INTERVAL_EXPRESSION_ERROR, REGEX, READHEAD - 2);
      }
      if(min == 0 && max == 1) {
        // <expression>{0,1} ; equivalent to <expression>?
        push(parser->symbol_stack, new_qmark_nfa(target));
        ++(parser->interval_count);
      }
      else if(min == 1 && max == 1) {
        // return unmodified target
        push(parser->symbol_stack, target);
      }
      else {
        if((min > 0 && max == 0) == 0) {
          // <expression>{,Max} ;match between 0 and Max
          // <expression>{Min,Max} ;match between Min and Max
          min = (min >= 0) ? min : 0;
        } // else <expression>{Min,} ;match at least Min, at most Infinity
        if(merge_intervals(parser, min, max, target) == 0) {
          interval_nfa = new_interval_nfa(target, min, max, &(parser->prev_interval_head), &(parser->prev_interval));
          ++(parser->interval_count);
          push(parser->symbol_stack, interval_nfa);
        }
      }
    }
    else {
      if(min == 1 && max == -1) {
        // <expression>{1,} ; equivalent to <expression>+
        push(parser->symbol_stack, new_posclosure_nfa(target));
      }
      else if(min > 0 && max == -1) {
        // <expression>{M}
        interval_nfa = new_interval_nfa(target, min, max, &(parser->prev_interval_head), &(parser->prev_interval));
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
        nfa_dealloc(parser->nfa_ctrl->alloc, target);
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
    parser_fatal(PROGRAM_NAME, MISSING_CLOSE_BRACE_ERROR, REGEX, READHEAD);
  }
DONT_COUNT_LOOP:
  return;
#undef DISCARD_WHITESPACE
#undef CONVERT_TO_UINT
}


static void
parse_quantifier_expression(Parser * parser)
{
  NFA * nfa;

  switch(parser->lookahead.type) {
    case PLUS: {
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      nfa = new_posclosure_nfa(nfa);
      push(parser->symbol_stack, nfa);
      parse_quantifier_expression(parser);
    } break;
    case KLEENE: {
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      nfa = new_kleene_nfa(nfa);
      push(parser->symbol_stack, nfa);
      parse_quantifier_expression(parser);
    } break;
    case QMARK: {
      parser_consume_token(parser);
      nfa = pop(parser->symbol_stack);
      nfa = new_qmark_nfa(nfa);
      push(parser->symbol_stack, nfa);
      parse_quantifier_expression(parser);
    } break;
    case OPENBRACE: {
      parse_interval_expression(parser);
      parse_quantifier_expression(parser);
    } break;
    case PIPE: {
      parser->tree_count += (parser->in_alternation) ? 0 : 1;
      parser->branch += 1;
      ++(parser->in_alternation);
      //parser->lowest_id_on_branch = parser->nfa_ctrl->next_seq_id;
      parser->lowest_id_on_branch = NEXT_NFA_ID(parser->nfa_ctrl);
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
        push(parser->branch_stack, right); // FIXME: LEAK!! -- Need to investigate
      }

      --(parser->in_alternation);

      if(parser->in_alternation == 0) {
        unsigned int sz = list_size(parser->branch_stack);
        left = new_alternation_nfa(parser->nfa_ctrl, parser->branch_stack, sz, NULL);
        parser->subtree_branch_count = 0;
        push(parser->symbol_stack, left);
        parser->branch = 0;
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


static void
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
      parser_fatal(PROGRAM_NAME, MALFORMED_COLLATION_EXPRESSION_ERROR, REGEX, READHEAD);
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
          //fatal(UNKNOWN_CHARCLASS_ERROR);
          parser_fatal(PROGRAM_NAME, UNKNOWN_CHARCLASS_ERROR, REGEX, READHEAD);
        }
      } break;
      case DOT: {
        warn("Collating-symbols not supported\n");
      } break;
      case EQUAL: {
        warn("Equivalence-calss expressions not supported\n");
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
          // we know the range is in the lowercase alphabetic characters
          if(prev_token.value > lookahead.value) {
            parser_fatal(PROGRAM_NAME, INVALID_RANGE_EXPRESSION_ERROR, REGEX, READHEAD);
          }
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
        parser_fatal(PROGRAM_NAME, INVALID_RANGE_EXPRESSION_ERROR, REGEX, READHEAD);
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


static void
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

  NFA * range_nfa = new_range_nfa(parser->nfa_ctrl, negate_match);
  push(parser->symbol_stack, range_nfa);

  parse_matching_list(parser, range_nfa->parent, negate_match);

  if(parser->lookahead.type != CLOSEBRACKET) {
    parser_fatal(PROGRAM_NAME, MISSING_CLOSE_BRACKET_ERROR, REGEX, READHEAD);
  }
  else {
    // resets charclass array
    token_in_charclass(0, 1);

    // re-enable scanning escape sequences
    SET_ESCP_FLAG(parser->ctrl_flags);

    parser_consume_token(parser);
    NFA * right = pop(parser->symbol_stack);
/*
    if(right == open_delim_p) {
      //fatal(EMPTY_BRACKET_EXPRESSION_ERROR);
      parser_fatal(PROGRAM_NAME, EMPTY_BRACKET_EXPRESSION_ERROR, REGEX, READHEAD);
    }
*/
    NFA * left  = pop(parser->symbol_stack); //NULL;

    if(left != open_delim_p) {
      fatal("Error parsing bracket expression\n");
    }

    push(parser->symbol_stack, right);
    parse_quantifier_expression(parser);
  }
}


static void
parse_literal_expression(Parser * parser)
{
  NFA * nfa = NULL;
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
           case ASCIIDIGIT: // fall-through
           case ALPHA: {
             if(next.value == '\\') {
               parser_backtrack(parser);
               stop = 1;
               --len;
               continue;
             }
           } break;
           case OPENBRACE:
           case KLEENE: // fall-through
           case QMARK:  // fall-through
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
         nfa = concatenate_nfa(nfa, new_lliteral_nfa(parser->nfa_ctrl, src, len));
       }
       else {
         c = src[0];
         nfa = concatenate_nfa(nfa, new_literal_nfa(parser->nfa_ctrl, c, NFA_LITERAL));
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

  (parser->paren_stack)[parser->paren_idx] = parser->cgrp_count;
  ++(parser->paren_idx);

  track_capture_group(parser, NFA_CAPTUREGRP_BEGIN);
  subtree_branch_count = parser->subtree_branch_count;
  parser->subtree_branch_count = 0;
  return subtree_branch_count;
}


void
update_close_paren_accounting(Parser * parser, unsigned int subtree_br_cnt)
{
  parser->subtree_branch_count = subtree_br_cnt;
  --(parser->paren_idx);
}


static void
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
      parser_fatal(PROGRAM_NAME, MISSING_CLOSE_PAREN_ERROR, REGEX, READHEAD);
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
    parser_fatal(PROGRAM_NAME, MISSING_CLOSE_PAREN_ERROR, REGEX, READHEAD);
  }

  return;
}


static void
validate_backref(Parser * parser)
{
  int val = parser->lookahead.value;
  // Handle cases:
  //   - backreference is an invalid value
  //   - backreference is in the capture-group it references
  //   - backreference precedes the capture-group.
  if((val == 0)
  || (val > CGRP_MAX)
  || (parser->capgrp_record[val - 1].end == NULL)) {
    parser_fatal(PROGRAM_NAME, INVALID_BACKREF_ERROR, REGEX, READHEAD);
  }

  if(parser->in_alternation) {
    // backreference is in an alternation/tree
    NFA * tmp = list_get_tail(parser->branch_stack);
    int lowest_id_on_tree = tmp->parent->id;
    int first_id_in_cgrp = parser->capgrp_record[val - 1].next_id;
    if(lowest_id_on_tree <= first_id_in_cgrp) {
      // capture-group is in tree being processed
      if(first_id_in_cgrp < parser->lowest_id_on_branch)  {
        // capture-group and backref are on different branches
        parser_fatal(PROGRAM_NAME, INVALID_BACKREF_ERROR, REGEX, READHEAD);
      }
    }
  }


}


static void
parse_sub_expression(Parser * parser)
{
  NFA * right = NULL;
  NFA * left  = NULL;
RETRY_PARSE:
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
      validate_backref(parser);
      left = new_backreference_nfa(parser->nfa_ctrl, parser->lookahead.value - 1);
      push(parser->symbol_stack, left);
      parser_consume_token(parser);
      parse_quantifier_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case CLOSEBRACE: {
      parser_fatal(PROGRAM_NAME, MISSING_OPEN_BRACE_ERROR, REGEX, READHEAD);
    } break;
    case CLOSEBRACKET: {
      parser_fatal(PROGRAM_NAME, MISSING_OPEN_BRACKET_ERROR, REGEX, READHEAD);
    } break;
    case HYPHEN: {
      parser->lookahead.type = ALPHA;
      goto RETRY_PARSE;
    } break;
/*  FIXME: uncomment when these are fully implemented
    case WORD_BOUNDARY: {
      negate = 1;
    } // fall-through
    case NOT_WORD_BOUNDARY: {
      type = (negate == 1) ? NFA_WORD_BOUNDARY : NFA_NOT_WORD_BOUNDARY;
      right = new_literal_nfa(parser->nfa_ctrl, 0, type);
      push(parser->symbol_stack, right);
      parser_consume_token(parser);
      parse_quantifier_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case AT_WORD_BEGIN:  {
      negate = 1; 
    } // fall-through
    case AT_WORD_END: {
      // Match the empty string at the end of word.
      // during recognition.. check lookahead for [[:space:]]
      type = (negate == 0) ? NFA_WORD_END_ANCHOR : NFA_WORD_BEGIN_ANCHOR;
      right = new_literal_nfa(parser->nfa_ctrl, 0, type);
      push(parser->symbol_stack, right);
      parser_consume_token(parser);
      parse_quantifier_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case NOT_WORD_CONSTITUENT: // synonym for '[^_[:alnum:]]'
      negate = 1;
    // fall-through
    case WORD_CONSTITUENT: { // synonym for '[_[:alnum:]]'.
      right = new_range_nfa(parser->nfa_ctrl, negate);
      update_range_w_collation("alnum", 5, right->parent, negate);
      update_range_nfa('_', '_', right->parent, negate);
      push(parser->symbol_stack, right);
      parser_consume_token(parser);
      parse_quantifier_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
    case NOT_WHITESPACE: // synonym for '[[:space:]]'
    negate = 1;
    // fall-through
    case WHITESPACE: { // synonym for '[^[:space:]]'.
      right = new_range_nfa(parser->nfa_ctrl, negate);
      update_range_w_collation("space", 5, right->parent, negate);
      push(parser->symbol_stack, right);
      parser_consume_token(parser);
      parse_quantifier_expression(parser);
      parse_sub_expression(parser);
      right = pop(parser->symbol_stack);
      left = pop(parser->symbol_stack);
      push(parser->symbol_stack, concatenate_nfa(left, right));
    } break;
*/
    case CLOSEPAREN:
      if(parser->paren_count == 0) {
        parser_fatal(PROGRAM_NAME, MISSING_OPEN_PAREN_ERROR, REGEX, READHEAD);
      } // fallthrough
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
  || parser->lookahead.type == OPENPAREN
/* FIXME: uncomment when these are fully implemented
  || parser->lookahead.type == WORD_BOUNDARY
  || parser->lookahead.type == NOT_WORD_BOUNDARY
  || parser->lookahead.type == AT_WORD_BEGIN
  || parser->lookahead.type == AT_WORD_END
  || parser->lookahead.type == WORD_CONSTITUENT
  || parser->lookahead.type == NOT_WORD_CONSTITUENT
  || parser->lookahead.type == WHITESPACE
  || parser->lookahead.type == NOT_WHITESPACE
*/
  ) {
    parse_sub_expression(parser);
  }
  else {
    parser_fatal(PROGRAM_NAME, INVALID_PARSE_START_ERROR, REGEX, READHEAD);
  }
}


// Pre-scan the input regex looking for '\<n>' where <n> is the
// number of the capture-group referenced by the backreference
static int
prescan_input(Parser * parser)
{
  // FIXME don't always use alloc. Have the scanner keep a large enough buffer
  // to hold several scanner's buffers ?
  char * tmp_buffer = malloc(parser->scanner->buf_len);

  if(tmp_buffer == NULL) {
    fatal("Insufficient virtual memory\n");
  }
  memcpy(tmp_buffer, parser->scanner->buffer, parser->scanner->buf_len);


  Scanner * new_scanner_state = init_scanner(
    get_filename(parser->scanner),
    tmp_buffer,
    parser->scanner->buf_len,
    parser->scanner->line_len,
    parser->scanner->ctrl_flags
  );

  scanner_push_state(&(parser->scanner), new_scanner_state);

  char ** next = &(parser->scanner->readhead);
  int eol = parser->scanner->eol_symbol;
  int paren_count = 0;
  int token_count = 0;

  if((*next)[0] != eol) {
    char c = next_char(parser->scanner);
    while(c != eol) {
      ++token_count;
      switch(c) {
        case '\\': {
          c = next_char(parser->scanner);
          if(isdigit(c)) {
            mark_closure_map_backref(parser->cgrp_map, c - '0');
          }
          c = next_char(parser->scanner);
          continue;
        } break;
        case '(' : {
          ++paren_count;
        } // fall through
        default: {
          c = next_char(parser->scanner);
        } break;
      }
    }
  }
  if(paren_count != 0) {
    parser->paren_stack = xmalloc(sizeof(int)*paren_count);
  }

  new_scanner_state = scanner_pop_state(&(parser->scanner));
  free_scanner(new_scanner_state);

  // token_count holds a rough approximate of the total number
  // of nfa nodes we will need for this regex
  nfa_alloc_init(parser->nfa_ctrl, token_count);

  return 1;
}


static void
insert_epsilon_start(Parser * parser)
{
  NFA * start_nfa = (((NFA *)peek(parser->symbol_stack))->parent);
  if(start_nfa->value.type  & ~(NFA_SPLIT|NFA_EPSILON)) {
    NFA * start_node = new_nfa(parser->nfa_ctrl, NFA_EPSILON);
    NFA * right = pop(parser->symbol_stack);
    start_node->out1 = start_node->out2 = right->parent;
    right->parent = start_node;
    push(parser->symbol_stack, right);
  }
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
    parser->total_nfa_ids = NEXT_NFA_ID(parser->nfa_ctrl);
    insert_epsilon_start(parser);
    compile_regex(parser);

    ret = 1;
  }

  return ret;
}


void
parser_free(Parser * parser)
{
//  free_nfa(((NFA *)peek(parser->symbol_stack)));
  free_nfa(&(parser->nfa_ctrl));
  stack_delete((parser->symbol_stack), NULL);
  stack_delete((parser->branch_stack), NULL);
  list_free(parser->synth_patterns, (void *)free);
  free(parser->nfa_ctrl);
  free(parser->paren_stack);
  free(parser);
}
