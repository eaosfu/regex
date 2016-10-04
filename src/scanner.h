#ifndef SCANNER_H_
#define SCANNER_H_

#include "token.h"
/*
                          ESCP_FLAG--. 
                   MGLOBAL_FLAG---.  |
                     EOL_FLAG---.  | |
             AT_BOL_FLAG___      | | |
                           \     | | |
                            V    v v v
   00000000 00000000 00000000 00000000
                           A    A A A
              AT_EOL_FLAG_/     | | |
                     BOL_FLAG___/ | |
                     UNPUT_FLAG___/ |
                     NO_CASE_FLAG___/

*/

#define ESCP_SEQ_FLAG 0x001
#define NO_CASE_FLAG  0x002
#define MGLOBAL_FLAG  0x004
#define UNPUT_FLAG    0x008
#define EOL_FLAG      0x010
#define BOL_FLAG      0x020
#define AT_EOL_FLAG   0x100
#define AT_BOL_FLAG   0x200
#define REVERSE_FLAG  0x400

#define PARSE_ESCP_SEQ(ctrl_flags)  (ctrl_flags & ESCP_SEQ_FLAG)

// These control the behaviour of the scanner
#define SET_REVERSE_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | REVERSE_FLAG)
#define SET_ESCP_FLAG(ctrl_flags)      (*(ctrl_flags) = *(ctrl_flags) | ESCP_SEQ_FLAG)
#define SET_UNPUT_FLAG(ctrl_flag)      (*(ctrl_flag)  = *(ctrl_flag)  | UNPUT_FLAG)
#define CLEAR_UNPUT_FLAG(ctrl_flag)    (*(ctrl_flag)  = *(ctrl_flag)  & ~UNPUT_FLAG)
#define CLEAR_ESCP_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) & ~ESCP_SEQ_FLAG)
#define CLEAR_REVERSE_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~REVERSE_FLAG)

// These control the nfa_sim's matching behavour
// Note that once set these are never cleared
#define SET_BOL_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) | BOL_FLAG)
#define SET_EOL_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) | EOL_FLAG)
#define SET_NO_CASE_FLAG(ctrl_flags)     (*(ctrl_flags) = *(ctrl_flags) | NO_CASE_FLAG)
#define SET_MGLOBAL_FLAG(ctrl_flags)     (*(ctrl_flags) = *(ctrl_flags) | MGLOBAL_FLAG)

// These complement the BOL and EOL flags that control the nfa_sim
#define SET_AT_BOL_FLAG(ctrl_flags)     (*(ctrl_flags) = *(ctrl_flags) | AT_BOL_FLAG)
#define CLEAR_AT_BOL_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~AT_BOL_FLAG)
#define SET_AT_EOL_FLAG(ctrl_flags)     (*(ctrl_flags) = *(ctrl_flags) | AT_EOL_FLAG)
#define CLEAR_AT_EOL_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~AT_EOL_FLAG)

// If EOL_FLAG is set but the BOL_FLAG isn't then
// we need the scanner to read the buffer from end to start
#define REVERSE(ctrl_flags) \
  (((ctrl_flags) & EOL_FLAG) && !((ctrl_flags) & (BOL_FLAG)))

typedef unsigned int ctrl_flags;

typedef struct Scanner {
  int line_no;
  long int line_len;
  unsigned long int buf_len;
  unsigned long int bytes_read;
  ctrl_flags ctrl_flags;
  char * buffer;
  char * readhead;
  char * str_begin;
  char * last_newline;
  int eol_symbol;
  Token * curtoken;
} Scanner;


Token * scanner_curtoken(Scanner *);
Token * regex_scan(Scanner *);
Scanner * new_scanner();
void unput(Scanner *);
void reset(Scanner *);
void init_scanner(Scanner *, char * buffer, unsigned int, unsigned int);
void free_scanner(Scanner *);
void check_match_anchors(Scanner *);
void restart_from(Scanner *, char *);
void reset_scanner(Scanner *);
int next_char(Scanner *);
#endif
