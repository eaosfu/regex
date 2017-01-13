#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "token.h"
#include "misc.h"


Token *
new_token()
{
  Token * t = xmalloc(sizeof(*t));
  return t;
}


void
update_token(Token * t, unsigned int c, symbol_type type)
{
  t->type   = type;
  t->value  = c;
  return;
}

void
free_token(Token * t)
{
  if(t != NULL) {
    free(t);
  }
  t = NULL;
}


void
print_token(symbol_type s)
{
  switch(s) {
    case ALPHA: printf("ALPHA\n"); break;
    case CLOSEPAREN: printf("CLOSEPAREN"); break;
    case HYPHEN: printf("HYPHEN"); break;
    case KLEENE: printf("*"); break;
    case NEWLINE: printf("NEWLINE\n"); break;
    case OPENPAREN: printf("OPENPAREN"); break;
    case PIPE: printf("|"); break;
    case PLUS: printf("+"); break;
    case QMARK: printf("?"); break;
//    case SPACE: printf("SPACE"); break;
//    case TAB: printf("TAB"); break;
    case __EOF: printf("EOF\n"); break;
    default: {
      printf("Unknown token type\n");
    } break;
  }
    
}
