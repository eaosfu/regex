#include <stdio.h>
#include <string.h>
#include <error.h>
#include <stdlib.h>
#include "scanner.h"
#include "misc.h"

static inline void
update_flags(Scanner * s)
{
  if(s && s->buffer) {
    if((s->readhead == s->buffer) || (s->readhead - s->last_newline) == 1) {
      SET_AT_BOL_FLAG(&s->ctrl_flags);
    }
    else {
      CLEAR_AT_BOL_FLAG(&s->ctrl_flags);
    }
    if((s->readhead[0] == s->eol_symbol) || s->readhead[0] == '\n') {
      SET_AT_EOL_FLAG(&s->ctrl_flags);
    }
    else {
      CLEAR_AT_EOL_FLAG(&s->ctrl_flags);
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
    //free(s->curtoken);
    free_token(s->curtoken);
    s->curtoken = NULL;
  }
  free(s);
}


Scanner *
new_scanner()
{
  struct Scanner * s = xmalloc(sizeof * s);
  s->curtoken = new_token();
  return s;
}


void
init_scanner(Scanner * s, char * buffer, unsigned int buffer_len, unsigned line_len)
{
  buffer[buffer_len - 1] = '\0';
  s->buf_len = buffer_len;
  s->line_len = line_len; 
  s->buffer = buffer;
  s->eol_symbol = (EOF < -1) ? -1 : -2;
  reset_scanner(s);
}


void
reset_scanner(Scanner * s)
{
  s->last_newline = 0;
  //s->str_begin = s->bol = s->readhead = s->buffer;
  s->str_begin = s->readhead = s->buffer;
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
  SET_ESCP_FLAG(&s->ctrl_flags);
  update_flags(s);
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
    SET_AT_EOL_FLAG(&s->ctrl_flags);
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


// TODO: reverse meaning of escp so that \(, \[, \+, etc.
//       are treated as operators rather than literals
Token *
regex_scan(Scanner * s)
{
  int ret = 0;
  int c;

// FOR TESTING
  static int seen_newline = 0;
  if(seen_newline) {
    update_token(s->curtoken, 0, __EOF);
    return s->curtoken;
  }
// FOR TESTING DONE

  while(ret != 1 && (c = next_char(s)) != s->eol_symbol) {
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
      case '\n': { /* TESTING ONLY */seen_newline = 1; /*DONE TESTING*/}// fallthrough
      case EOF: update_token(s->curtoken, 0, __EOF);        ret = 1; break;
      case '$': update_token(s->curtoken, c, DOLLAR);       ret = 1; break;
      case '^': update_token(s->curtoken, c, CIRCUMFLEX);   ret = 1; break;
      case '.': update_token(s->curtoken, c, DOT);          ret = 1; break;
      case '*': update_token(s->curtoken, c, KLEENE);       ret = 1; break;
      case '+': update_token(s->curtoken, c, PLUS);         ret = 1; break;
      case ':': update_token(s->curtoken, c, COLON);         ret = 1; break;
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
        //if(s->parse_escp_seq) {
        if(PARSE_ESCP_SEQ(s->ctrl_flags)) {
          if((c = next_char(s)) != s->eol_symbol) {
            update_token(s->curtoken, c, ALPHA);
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

