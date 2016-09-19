#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"
#include "scanner.h"
#include "token.h"

#define REMAINING_BUFFER (MAX_BUFFER_SZ - 1 - s->bytes_read)

Scanner *
init_scanner(FILE * stream)
{
printf("SCANNER INIT START\n");
  Scanner * s = xmalloc(sizeof(Scanner));
  s->input = stream;
  s->unput = 0;
  s->parse_escp_seq = 1;
  memset(s->buffer, 0, MAX_BUFFER_SZ);
  s->readhead = s->buffer;
  s->curtoken = new_token();
printf("SCANNER INIT END\n");
  return s;
}


void
reset(Scanner *s)
{
  if(s != NULL)
    s->readhead = NULL;
}


static inline int
next_char(Scanner * s)
{
  int nc = EOF;
  if(s != NULL) {
    if(REMAINING_BUFFER > 0) {
      if(s->unput) {
        nc = *++(s->readhead);
        s->unput = 0;
      }
      else {
        // move readhead forward and update buffer
        nc = *++(s->readhead) = fgetc(s->input);
        ++(s->bytes_read);
      }
    }
  }
  return nc;
}


void
unput(Scanner * s)
{
  if(s != NULL)
    if(s->bytes_read > 0) {
      *--(s->readhead);
      s->unput = 1;
    }
}


// TODO: reverse meaning of escp so that \(, \[, \+, etc.
//       are treated as operators rather than literals
Token *
scan(Scanner * s)
{
  int ret = 0;
  unsigned int c;

// FOR TESTING
  static int seen_newline = 0;
  if(seen_newline) {
    update_token(s->curtoken, 0, __EOF);
    return s->curtoken;
  }
// FOR TESTING DONE

  while(ret != 1 && (c = next_char(s))) {
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
        if(s->parse_escp_seq) {
          if((c = next_char(s)) != EOF) {
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

  return s->curtoken;
}

void
free_scanner(Scanner * s)
{
  if(s == NULL) {
    return;
  }

  fclose(s->input);
  s->readhead  = NULL;

  if(s->curtoken != NULL) {
    free(s->curtoken);
    s->curtoken = NULL;
  }
  free(s);
}
