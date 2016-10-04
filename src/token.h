#ifndef TOKEN_H_
#define TOKEN_H_

typedef enum {
  ALPHA, 
  ASCIIDIGIT,
  CIRCUMFLEX,
  CLOSEBRACE,
  CLOSEBRACKET,
  CLOSEPAREN,
  COLON,
  DOT,
  DOLLAR,
  EMPTY,
  EQUAL,
  HYPHEN,
  KLEENE,
  NEWLINE, 
  OPENBRACE,
  OPENBRACKET,
  OPENPAREN,
  PIPE,
  PLUS,
  QMARK,
  SPACE,
  TAB,
  __EOF
} symbol_type;

typedef struct Token {
  symbol_type type;
  unsigned length;
  unsigned int value;
} Token;

Token * new_token();
void update_token(Token *, unsigned int, symbol_type);
void free_token(Token *);

void print_token(symbol_type);
#endif
