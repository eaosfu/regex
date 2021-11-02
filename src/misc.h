#ifndef MISC_H_
#define MISC_H_

#include <err.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>

// SHARED BETWEEN PARSER, SCANNER AND RECOGNIZER
#define EOL_FLAG              0x00001
#define BOL_FLAG              0x00002
#define UNPUT_FLAG            0x00004
#define AT_EOL_FLAG           0x00008
#define AT_BOL_FLAG           0x00010
#define ESCP_SEQ_FLAG         0x00020
// TAKE EFFECT AT RUNTIME
#define MGLOBAL_FLAG          0x00040
#define SHOW_LINENO_FLAG      0x00080
#define SHOW_FILE_NAME_FLAG   0x00100
#define IGNORE_CASE_FLAG      0x00200
#define SILENT_MATCH_FLAG     0x00400
#define INVERT_MATCH_FLAG     0x00800
#define SHOW_MATCH_LINE_FLAG  0x01000

// Access the control flags
#define CTRL_FLAGS(s) (*((s)->ctrl_flags))

// This allows the parser to control how the scanner interprets 
// tokens that begin with a '\'
#define PARSE_ESCP_SEQ(ctrl_flags)  (*(ctrl_flags) & ESCP_SEQ_FLAG)

// These control the behaviour of the scanner
#define SET_UNPUT_FLAG(ctrl_flag)      (*(ctrl_flag)  = *(ctrl_flag)  | UNPUT_FLAG)
#define CLEAR_UNPUT_FLAG(ctrl_flag)    (*(ctrl_flag)  = *(ctrl_flag)  & ~UNPUT_FLAG)
#define SET_ESCP_FLAG(ctrl_flags)      (*(ctrl_flags) = *(ctrl_flags) | ESCP_SEQ_FLAG)
#define CLEAR_ESCP_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) & ~ESCP_SEQ_FLAG)

#define SET_AT_BOL_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) | AT_BOL_FLAG)
#define CLEAR_AT_BOL_FLAG(ctrl_flags)  (*(ctrl_flags) = *(ctrl_flags) & ~AT_BOL_FLAG)
#define SET_AT_EOL_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) | AT_EOL_FLAG)
#define CLEAR_AT_EOL_FLAG(ctrl_flags)  (*(ctrl_flags) = *(ctrl_flags) & ~AT_EOL_FLAG)

// These control the nfa_sim's matching behavour
// Note that once set these are never cleared
#define SET_MGLOBAL_FLAG(ctrl_flags)          (*(ctrl_flags) = *(ctrl_flags) | MGLOBAL_FLAG)
#define CLEAR_MGLOBAL_FLAG(ctrl_flags)        (*(ctrl_flags) = *(ctrl_flags) & ~MGLOBAL_FLAG)

// ENGINE DOESN'T SUPPORT THIS YET
#define SET_SHOW_LINENO_FLAG(ctrl_flags)      (*(ctrl_flags) = *(ctrl_flags) | SHOW_LINENO_FLAG)
#define CLEAR_SHOW_LINENO_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) & ~SHOW_LINENO_FLAG)

#define SET_IGNORE_CASE_FLAG(ctrl_flags)      (*(ctrl_flags) = *(ctrl_flags) | IGNORE_CASE_FLAG)
#define CLEAR_IGNORE_CASE_FLAG(ctrl_flags)    (*(ctrl_flags) = *(ctrl_flags) & ~IGNORE_CASE_FLAG)

#define SET_SHOW_FILE_NAME_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | SHOW_FILE_NAME_FLAG)
#define CLEAR_SHOW_FILE_NAME_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~SHOW_FILE_NAME_FLAG)

#define SET_SILENT_MATCH_FLAG(ctrl_flags)     (*(ctrl_flags) = *(ctrl_flags) | SILENT_MATCH_FLAG)
#define CLEAR_SILENT_MATCH_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) & ~SILENT_MATCH_FLAG)

#define SET_INVERT_MATCH_FLAG(ctrl_flags)     (*(ctrl_flags) = *(ctrl_flags) | INVERT_MATCH_FLAG)
#define CLEAR_INVERT_MATCH_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) & ~INVERT_MATCH_FLAG)

#define SET_SHOW_MATCH_LINE_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | SHOW_MATCH_LINE_FLAG)
#define CLEAR_SHOW_MATCH_LINE_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~SHOW_MATCH_LINE_FLAG)

typedef unsigned int ctrl_flags;

void fatal(const char *);
void * xmalloc(unsigned int);
void parser_fatal(const char *, const char *, const char *, const char *);
size_t round_to_page(size_t size);

#endif
