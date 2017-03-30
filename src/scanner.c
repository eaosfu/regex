#include "scanner.h"

#include <stdio.h>
#include <string.h>
#include <error.h>
#include <stdlib.h>
#include <ctype.h>

// Need to modify how we declare this if we want to
// be able to use this in a parallel environment
static List G_STATE_STACK = {.head = NULL, .size = 0, .pool = NULL, .pool_size = 0};
static Stack * g_state_stack = &G_STATE_STACK;


const char *
get_buffer_end(Scanner * s)
{
  if(s) {
    return (s->buffer + s->line_len);
  }
  return NULL;
}


int
get_buffer_length(Scanner * s)
{
  if(s) {
    return s->buf_len;
  }
  return 0;
}


const char *
get_buffer_start(Scanner * s)
{
  if(s) {
    return s->buffer;
  }
  return NULL;
}


const char *
get_filename(Scanner * s)
{
  if(s && s->filename) {
    return s->filename;
  }
  return NULL;
}


int
get_line_no(Scanner * s)
{
  if(s) {
    return s->line_no;
  }
  return 0;
}

static inline void
update_flags(Scanner * s)
{
  if(s && s->buffer) {
    if((s->readhead == s->buffer) || (s->readhead - s->last_newline) == 1) {
      SET_AT_BOL_FLAG(s->ctrl_flags);
    }
    else {
      CLEAR_AT_BOL_FLAG(s->ctrl_flags);
    }
    if((s->readhead[0] == s->eol_symbol) || s->readhead[0] == '\n') {
      SET_AT_EOL_FLAG(s->ctrl_flags);
    }
    else {
      CLEAR_AT_EOL_FLAG(s->ctrl_flags);
    }
  }
}


void
free_scanner(Scanner * s)
{
  if(s == NULL) {
    return;
  }

  if(s->buffer) {
    free(s->buffer);
  }

  s->readhead  = NULL;

  if(s->curtoken != NULL) {
    free_token(s->curtoken);
    s->curtoken = NULL;
  }
  
  free(s);

  s = NULL;

  return;
}


// Returns the current scanner state to the caller, and sets the
// current scanner state to the top of the G_STATE_STACK. The top
// of said stack is popped.
Scanner *
scanner_pop_state(Scanner ** s)
{
  Scanner * ret = *s;
  if(*s) {
    Scanner * prev_state = pop(g_state_stack);
    if(prev_state) {
      *s = prev_state;
    }
    else {
      *s = NULL;
    }
  }
  return ret;
}


// Stores the current scanner in the G_STATE_STACK
// and sets the current scanner to 'new_scanner'
// returns the new size of the G_STATE_STACK
int
scanner_push_state(Scanner ** old_state, Scanner * new_state)
{
  int ret = 0;
  if(*old_state) {
    ret = push(g_state_stack, (*old_state));
  }
  (*old_state) = new_state;
  return ret;
}


Scanner *
init_scanner(const char * filename, char * buffer, unsigned int buffer_len, unsigned int line_len, ctrl_flags * cfl)
{
  struct Scanner * s = xmalloc(sizeof * s);
  s->curtoken    = new_token();
  buffer[buffer_len - 1] = '\0';
  s->filename   = filename;
  s->buf_len    = buffer_len;
  s->buffer     = buffer;
  s->line_len   = line_len; 
  s->ctrl_flags = cfl;
  s->eol_symbol = (EOF < -1) ? -1 : -2;
  reset_scanner(s, filename);
  return s;
}


void
reset_scanner(Scanner * s, const char * filename)
{
  s->last_newline = 0;
  if(s->buffer[s->line_len - 1] == '\n') {
    // replace newline with special EOL symbol
    s->buffer[s->line_len - 1] = s->eol_symbol;
  }
  else {
    // if buffer is only large enough to fit the null terminated
    // input string then s->line_len == s->buf_len - 1
    if(s->buf_len < s->line_len + 2) {
      s->buffer = realloc((void *)s->buffer, s->buf_len + 1);
      s->buf_len++;
    }
    s->buffer[s->line_len] = s->eol_symbol;
    s->buffer[s->line_len + 1] = '\0';
    s->line_len++;
  }
  s->str_begin = s->readhead = s->buffer;
  SET_ESCP_FLAG(s->ctrl_flags);
  update_flags(s);
  s->filename = filename;
}

char *
get_cur_pos(Scanner * s)
{
  char * ret = NULL;
  if(s) {
    ret = s->buffer;
    if(s->readhead > s->str_begin) {
      ret = s->readhead - 1;
    }
    else {
      ret = s->readhead;
    }
  }
  return ret;
}


void
unput(Scanner * s)
{
  if(s && (s->readhead - s->str_begin) > 0) {
    --s->readhead;
    if(s->readhead[0] == '\n') {
      --s->line_no;
    }
  }
}


void
restart_from(Scanner * s, char * pos)
{
  int offset = pos - s->buffer;
  if(offset >= 0 && offset < s->line_len) {
    s->str_begin = s->readhead = pos;
    update_flags(s);
  }
  else {
    s->readhead = s->buffer + (s->line_len);
    SET_AT_EOL_FLAG(s->ctrl_flags);
  }
}


int
next_char(Scanner * s)
{
  int nc = EOF;
  if(s) {
    if((nc = s->readhead[0]) != '\0') {
      update_flags(s);
      ++s->readhead;
    }
  }
  return nc;
}


char *
get_scanner_readhead(Scanner * s)
{
  char * ret = NULL;
  if(s && s->readhead) {
    ret = s->readhead;
  }
  return ret;
}


Token *
regex_scan(Scanner * s)
{
  int c;
  int ret = 0;
  
  // FIXME: would need to update how this is handled if we were to support
  //        multiline matching... not supported for now...
  static int seen_newline = 0;
  if(seen_newline) {
    update_token(s->curtoken, 0, __EOF);
    return s->curtoken;
  }

  while(ret != 1 && (c = next_char(s)) != '\0') {
    switch(c) {
      case '0' : // fallthrough
      case '1' : // fallthrough
      case '2' : // fallthrough
      case '3' : // fallthrough
      case '4' : // fallthrough
      case '5' : // fallthrough
      case '6' : // fallthrough
      case '7' : // fallthrough
      case '8' : // fallthrough
      case '9' : update_token(s->curtoken, c, ASCIIDIGIT);  ret = 1; break;
      case '\n': seen_newline = 1; // fallthrough
      case EOF: update_token(s->curtoken, 0, __EOF);        ret = 1; break;
      case '$': update_token(s->curtoken, c, DOLLAR);       ret = 1; break;
      case '^': update_token(s->curtoken, c, CIRCUMFLEX);   ret = 1; break;
      case '.': update_token(s->curtoken, c, DOT);          ret = 1; break;
      case '*': update_token(s->curtoken, c, KLEENE);       ret = 1; break;
      case '+': update_token(s->curtoken, c, PLUS);         ret = 1; break;
      case ':': update_token(s->curtoken, c, COLON);        ret = 1; break;
      case '|': update_token(s->curtoken, c, PIPE);         ret = 1; break;
      case '?': update_token(s->curtoken, c, QMARK);        ret = 1; break;
      case '(': update_token(s->curtoken, c, OPENPAREN);    ret = 1; break;
      case ')': update_token(s->curtoken, c, CLOSEPAREN);   ret = 1; break;
      case '-': update_token(s->curtoken, c, HYPHEN);       ret = 1; break;
      case '[': update_token(s->curtoken, c, OPENBRACKET);  ret = 1; break;
      case ']': update_token(s->curtoken, c, CLOSEBRACKET); ret = 1; break;
      case '{': update_token(s->curtoken, c, OPENBRACE);    ret = 1; break;
      case '}': update_token(s->curtoken, c, CLOSEBRACE);   ret = 1; break;
      case '\\': {
        if(PARSE_ESCP_SEQ(s->ctrl_flags)) {
          if((c = next_char(s)) != s->eol_symbol) {
            if(isdigit(c)) {
              update_token(s->curtoken, (c - '0'), BACKREFERENCE);
            }
            else {
              switch(c) {
                // FIXME: Insert escape sequences like '\d', '\b', '\D'
                default: {
                  update_token(s->curtoken, c, ALPHA);
                } break;
              }
            }
          }
          else {
            unput(s);
          }
        }
        else {
          update_token(s->curtoken, c, ALPHA);
        }
        ret = 1;
      } break;
      default: {
        update_token(s->curtoken, c, ALPHA);
        ret = 1;
      } break;
    }
  }

  if(c == s->eol_symbol) {
    update_token(s->curtoken, 0, __EOF);
  }

  return s->curtoken;
}

