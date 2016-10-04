#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "token.h"
#include "misc.h"


Token *
new_token()
{
  Token * t = xmalloc(sizeof(Token));
  return t;
}


void
update_token(Token * t, unsigned int c, symbol_type type)
{
  t->type = type;
  t->value = c;
  return;
}

void
free_token(Token * t)
{
  free(t);
  t = NULL;
}


void
print_token(symbol_type s)
{
  switch(s) {
    case ALPHA: printf("ALPHA\n"); break;
    case CLOSEPAREN: printf(""); break;
    case HYPHEN: printf(""); break;
    case KLEENE: printf("*"); break;
    case NEWLINE: printf("NEWLINE\n"); break;
    case OPENPAREN: printf(""); break;
    case PIPE: printf("|"); break;
    case PLUS: printf("+"); break;
    case QMARK: printf("?"); break;
    case SPACE: printf(""); break;
    case TAB: printf(""); break;
    case __EOF: printf("EOF\n"); break;
    default: {
      printf("Unknown token type\n");
    } break;
  }
    
}
