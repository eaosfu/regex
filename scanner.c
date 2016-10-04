#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc.h"
#include "scanner.h"
#include "token.h"

#define REMAINING_BUFFER(s) ((s)->buffer_len - s->bytes_read)

int
fill_buffer(Scanner * s)
{
  int buffer_len = 0;
  int eol_only = 0;
  char first, last, c;

printf("ORIGINAL REGEX: ");
  if((c = fgetc(s->input)) == EOF || c == '\n') {
    fatal("EMPTY REGULAR EXPRESSION\n");
  }

  (s->buffer)[buffer_len] = first = c;
  buffer_len++;
printf("%c", c);

  while(buffer_len < MAX_BUFFER_SZ - 1) {
    if((c = fgetc(s->input)) == EOF || c == '\n') {
      (s->buffer)[buffer_len] == '\n';
      break;
    }
printf("%c", c);
    (s->buffer)[buffer_len] = last = c;
    buffer_len++;
  }
printf("\n");

  s->buffer_len = buffer_len;
  int max_index = buffer_len - 1;
  int i = max_index;

  if(first != '^' && last == '$') {
    eol_only = 1;
    while(i > (max_index)/2) {
      last = (s->buffer)[i];
printf("SWAP FIRST: %c WITH LAST: %c\n", (s->buffer)[i], (s->buffer)[max_index - i]);
      (s->buffer)[i] = (s->buffer)[max_index - i];
      (s->buffer)[max_index - i] = last;
printf("RESULT SWAP FIRST: %c WITH LAST: %c\n", (s->buffer)[i], (s->buffer)[max_index - i]);
      --i;
    }
  }

  return eol_only;
}

Scanner *
init_scanner(FILE * stream)
{
printf("SCANNER INIT START\n");
  Scanner * s = xmalloc(sizeof(Scanner));
  s->input = stream;
  s->unput = 0;
  s->parse_escp_seq = 1;
  memset(s->buffer, 0, MAX_BUFFER_SZ);
  s->eol_only = fill_buffer(s);
  s->readhead = s->buffer;
  s->curtoken = new_token();
printf("SCANNER INIT END -- ");
printf("REGEX BUFFER CONTAINS: '%s'\n", s->readhead);
  return s;
}


void
reset(Scanner *s)
{
  if(s != NULL && s->buffer != NULL)
    s->readhead = s->buffer;
}


static inline int
next_char(Scanner * s)
{
  int nc = EOF;
  if(s != NULL) {
printf("REMAINING BUFFER: %d\n", REMAINING_BUFFER(s));
    if(REMAINING_BUFFER(s) > 0) {
      if(s->unput) {
        s->unput = 0;
        nc = *s->readhead;
        ++s->readhead;
      }
      else {
        // move readhead forward and update buffer
        nc = *(s->readhead);
        ++s->readhead;
        ++(s->bytes_read);
      }
    }
  }
printf("BYTES READ: %d vs. BUFFER_LEN: %d, CHAR: %c\n", s->bytes_read, s->buffer_len, nc);
  return nc;
}


void
unput(Scanner * s)
{
  if(s != NULL)
    if(s->bytes_read > 0) {
      --(s->readhead);
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
