#ifndef MISC_H_
#define MISC_H_

// SHARED BETWEEN PARSER, SCANNER AND RECOGNIZER
#define EOL_FLAG              0x001
#define BOL_FLAG              0x002
#define UNPUT_FLAG            0x004
#define AT_EOL_FLAG           0x008
#define AT_BOL_FLAG           0x010
#define ESCP_SEQ_FLAG         0x020
// TAKE EFFECT AT RUNTIME
#define MGLOBAL_FLAG          0x040
#define SHOW_LINE_FLAG        0x080
#define SHOW_FILE_NAME_FLAG   0x100
#define IGNORE_CASE_FLAG      0x200
#define SILENT_MATCH_FLAG     0x400
#define INVERT_MATCH_FLAG     0x800

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

#define SET_AT_BOL_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | AT_BOL_FLAG)
#define CLEAR_AT_BOL_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~AT_BOL_FLAG)
#define SET_AT_EOL_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | AT_EOL_FLAG)
#define CLEAR_AT_EOL_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & ~AT_EOL_FLAG)

// These control the nfa_sim's matching behavour
// Note that once set these are never cleared
#define SET_MGLOBAL_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | MGLOBAL_FLAG)
#define CLEAR_MGLOBAL_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & MGLOBAL_FLAG)

// ENGINE DOESN'T SUPPORT THIS YET
#define SET_SHOW_LINE_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | SHOW_LINE_FLAG)
#define CLEAR_SHOW_LINE_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & SHOW_LINE_FLAG)

#define SET_IGNORE_CASE_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | IGNORE_CASE_FLAG)
#define CLEAR_IGNORE_CASE_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & IGNORE_CASE_FLAG)

#define SET_SHOW_FILE_NAME_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | SHOW_FILE_NAME_FLAG)
#define CLEAR_SHOW_FILE_NAME_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & SHOW_FILE_NAME_FLAG)

#define SET_SILENT_MATCH_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | SILENT_MATCH_FLAG)
#define CLEAR_SILENT_MATCH_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & SILENT_MATCH_FLAG)

#define SET_INVERT_MATCH_FLAG(ctrl_flags)   (*(ctrl_flags) = *(ctrl_flags) | INVERT_MATCH_FLAG)
#define CLEAR_INVERT_MATCH_FLAG(ctrl_flags) (*(ctrl_flags) = *(ctrl_flags) & INVERT_MATCH_FLAG)


typedef unsigned int ctrl_flags;


void parser_fatal(const char *, const char *, const char *, int);
void fatal(const char *);
void warn(const char *);
void * xmalloc(unsigned int);
#endif
