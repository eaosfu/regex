#ifndef TOKEN_H_
#define TOKEN_H_

typedef enum {
  ALPHA, 
  ASCIIDIGIT,
  BACKREFERENCE,
  CIRCUMFLEX,
  CLOSEBRACE,
  CLOSEBRACKET,
  CLOSEPAREN,
  COLON,
  DOT,
  DOLLAR,
//  EMPTY,
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
//  SPACE,
//  TAB,
// TEST
  WORD_BOUNDARY,
  NOT_WORD_BOUNDARY,
  AT_WORD_BEGIN,
  AT_WORD_END,
  WORD_CONSTITUENT,
  NOT_WORD_CONSTITUENT,
  WHITESPACE,
  NOT_WHITESPACE,
// END TEST
  __EOF
} symbol_type;

typedef struct Token {
  symbol_type type;
  unsigned int length;
  unsigned int value;
} Token;

Token * new_token();
void update_token(Token *, unsigned int, symbol_type);
void free_token(Token *);

void print_token(symbol_type);
#endif
