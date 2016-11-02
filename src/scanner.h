#ifndef SCANNER_H_
#define SCANNER_H_

#include "stack.h"
#include "token.h"

#define ESCP_SEQ_FLAG      0x001
#define NO_CASE_FLAG       0x002
#define MGLOBAL_FLAG       0x004
#define UNPUT_FLAG         0x008
#define EOL_FLAG           0x010
#define BOL_FLAG           0x020
#define AT_EOL_FLAG        0x100
#define AT_BOL_FLAG        0x200
#define MARK_STATES_FLAG   0x400
#define FIX_STREAM_ID_FLAG 0x800 // parser specific flag

// Access the control flags
#define CTRL_FLAGS(s) (*((s)->ctrl_flags))

// Only used by the parser to assign unique 'stream' ids to nodes
// inside an alternation.
#define SET_FIX_STREAM_ID_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | FIX_STREAM_ID_FLAG)
#define CLEAR_FIX_STREAM_ID_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~FIX_STREAM_ID_FLAG)

// This allows the parser to control how the scanner interprets 
// tokens that begin with a '\'
#define PARSE_ESCP_SEQ(ctrl_flags)  (*(ctrl_flags) & ESCP_SEQ_FLAG)

// These control the behaviour of the scanner
#define SET_ESCP_FLAG(ctrl_flags)      (*(ctrl_flags) = *(ctrl_flags) | ESCP_SEQ_FLAG)
#define CLEAR_ESCP_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) & ~ESCP_SEQ_FLAG)
#define SET_UNPUT_FLAG(ctrl_flag)      (*(ctrl_flag)  = *(ctrl_flag)  | UNPUT_FLAG)
#define CLEAR_UNPUT_FLAG(ctrl_flag)    (*(ctrl_flag)  = *(ctrl_flag)  & ~UNPUT_FLAG)

// These control the nfa_sim's matching behavour
// Note that once set these are never cleared
#define SET_MGLOBAL_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | MGLOBAL_FLAG)
#define CLEAR_MGLOBAL_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & MGLOBAL_FLAG)

// ENGINE DOESN'T SUPPORT THIS YET
#define SET_NO_CASE_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | NO_CASE_FLAG)
#define CLEAR_NO_CASE_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~AT_EOL_FLAG)

#define SET_AT_BOL_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | AT_BOL_FLAG)
#define CLEAR_AT_BOL_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~AT_BOL_FLAG)
#define SET_AT_EOL_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | AT_EOL_FLAG)
#define CLEAR_AT_EOL_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~AT_EOL_FLAG)

#define SET_MARK_STATES_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | MARK_STATES_FLAG)
#define CLEAR_MARK_STATES_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~MARK_STATES_FLAG)

typedef unsigned int ctrl_flags;

typedef struct Scanner {
  int line_no;
  long int line_len;
  unsigned long int buf_len;
  ctrl_flags * ctrl_flags;
  char * buffer;
  char * readhead;
  char * str_begin; 
  char * last_newline;
  int eol_symbol;
  Token * curtoken;
} Scanner;


Token * regex_scan(Scanner *);
Scanner * init_scanner(char *, unsigned int, unsigned int, ctrl_flags *);
void unput(Scanner *);
void reset(Scanner *);
void free_scanner(Scanner *);
void restart_from(Scanner *, char *);
void reset_scanner(Scanner *);
int next_char(Scanner *);
char * get_scanner_readhead(Scanner *);
char * get_cur_pos(Scanner *);
int scanner_push_state(Scanner **, Scanner *);
Scanner * scanner_pop_state(Scanner **);
#endif
