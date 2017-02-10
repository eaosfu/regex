#include <getopt.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>
#include <limits.h>

#include "misc.h"
#include "scanner.h"
#include "backtrack_recognizer.h"


// This should be long enough.
char program_name[NAME_MAX];

static const char short_options [] = {"Ff:ghilqv"};

static struct option const long_options[] = {
  {"help"           , no_argument      , NULL, 'h'},
  {"global_match"   , no_argument      , NULL, 'g'},
  {"pattern-file"   , required_argument, NULL, 'f'},
  {"show-file-name" , no_argument      , NULL, 'F'}, // not implemented
  {"invert-match"   , no_argument      , NULL, 'v'}, // FIXME: issue with showing eol symbol
  {"show-match-line", no_argument      , NULL, 'l'},
  {"quiet"          , no_argument      , NULL, 'q'},
  {"silent"         , no_argument      , NULL, 'q'},
  {"ignore-case"    , no_argument      , NULL, 'i'}, // not implemented
  {0                , 0                , 0   , 0}
};


static void
print_description(void)
{
  printf("\n%s: Use regular expressions to search ""for <pattern> in <files>\n\n",
    program_name);
}


static void
print_usage(int exit_code)
{
  printf("usage: %s [options] '<regex>'  file [file]...\n", program_name);
  printf("       %s [options] -f <pattern_file> file [file]...\n", program_name);
  printf("\n");
  printf("options:\n"
"  -F, --show-file-name     display filename where match was found\n"
"  -f, --pattern-file       read regex pattern from a file\n"
"  -g, --global-match       find all matches on the input line\n"
"                           by default the first match stops the search\n"
"  -h, --help               display this help message\n"
"  -i, --ignore-case        treat all input, including pattern, as lowercase\n"
"  -l, --show-match-line    display line number for the match, starts at 1\n"
"  -q, --quiet, --silent    suppress all output to stdout\n"
"  -v, --invert-match       display lines where no matches are found\n"
"\n"
"Exit status is 1 if a match is found anywhere, 0 otherwise; unless an error\n"
"occurs, in which case the exit status will be set to 1.\n");
  printf("\n");
  exit(exit_code);
}


static void
print_help(void)
{
  print_description();
  print_usage(EXIT_SUCCESS);
}


static void
exit_unknown_opt(int opt, int exit_code)
{
  printf("Unknown option: %c\n", opt);
  print_usage(exit_code);
}


static void
exit_msg_usage(const char * msg, int exit_code)
{
  printf("\n%s\n\n", msg);
  print_usage(exit_code);
}

static void
set_program_name(char * name)
{
  char * nm = basename(name);
  int len  = strlen(nm);
  memcpy(program_name, nm, len);
  program_name[len] = '\0';
}


int
main(int argc, char ** argv)
{
  int status = 0;
  int opt = -1;

  ctrl_flags cfl = 0;
  Scanner * scanner = NULL;
  Parser  * parser  = NULL;
  NFASim  * nfa_sim = NULL;
  FILE    * fh    = NULL;
  char * filename = NULL;
  char * buffer = NULL;
  size_t buf_len = 0;
  unsigned int line_len = 0;

  set_program_name(argv[0]);

  int pattern_file_idx = 0;
  while((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
    switch(opt) {
      case  1 :// fallthrough
      case 'h': { print_help();                        } break;
      case 'f': { pattern_file_idx = optind - 1;       } break;
      case 'g': { SET_MGLOBAL_FLAG(&cfl);              } break;
      case 'i': { SET_IGNORE_CASE_FLAG(&cfl);          } break;
      case 'q': { SET_SILENT_MATCH_FLAG(&cfl);         } break;
      case 'v': { SET_INVERT_MATCH_FLAG(&cfl);         } break;
      case 'F': { SET_SHOW_FILE_NAME_FLAG(&cfl);       } break;
      case 'l': { SET_SHOW_LINENO_FLAG(&cfl);          } break;
      default:  { exit_unknown_opt(opt, EXIT_FAILURE); } break;
    }
  }

  // process remaining command line input
  int target_idx = optind;
  int cmd_regex_idx = 0;
  for(int i = optind; i < argc; ++i) {
    if(i == optind) {
      if(pattern_file_idx == 0) {
        cmd_regex_idx = i;
        ++target_idx;
        continue;
      }
    }
  }

  if(target_idx >= argc) {
    exit_msg_usage("ERROR: No search files provided", EXIT_FAILURE);
  }
  

  if(pattern_file_idx) {
    // read regex from pattern file
    fh = fopen(argv[pattern_file_idx], "r");
    if(fh == NULL) {
      fatal("Failed to open pattern file\n");
    }
    filename = argv[pattern_file_idx];
    line_len = getline(&buffer, &buf_len, fh);
  }
  else if(cmd_regex_idx) {
    //read regex from command line
    line_len = strlen(argv[cmd_regex_idx]);
    buf_len = line_len + 1;
    buffer = malloc(buf_len);
    strncpy(buffer, argv[cmd_regex_idx], line_len);
  }
  else {
    fatal("No regex pattern provided\n");
  }

  if(line_len < 0) {
    fatal("UNABLE READ REGEX FILE\n");
  }

  scanner  = init_scanner(filename, buffer, buf_len, line_len, &cfl);

  parser = init_parser(scanner, &cfl);

  if(parse_regex(parser) == 0) {
    goto CLEANUP;
  }

  if(fh) {
    fclose(fh);
  }
  

  // if we've made it this far we have all we need to run the recognizer
  while(target_idx < argc) {
    fh = fopen(argv[target_idx], "r");
    if(fh == NULL) {
      fatal("Unable to open file\n");
    }
    filename = argv[target_idx];
    int line = 0;
    nfa_sim = new_nfa_sim(parser, scanner, &cfl);
    NFASimCtrl * thread_ctrl = nfa_sim->ctrl;
    while((scanner->line_len = getline(&scanner->buffer, &scanner->buf_len, fh)) > 0) {
      reset_scanner(scanner, filename);
      reset_nfa_sim(nfa_sim, ((NFA *)peek(parser->symbol_stack))->parent);
// TEST: FIXME: NEED A BETTER INTERFACE FOR THIS... THE SCANNER SHOULD PROBABLY HANDLE
//              MOST/ALL INTERACTION WITH FILE/INPUT?
      ++line;
      scanner->line_no = line;
// TEST
      run_nfa(nfa_sim);
      nfa_sim = list_shift(thread_ctrl->thread_pool);
//      nfa_sim->prev_thread = nfa_sim->next_thread = nfa_sim;
    }

    if(thread_ctrl->match_idx) {
      flush_matches(thread_ctrl);
    }
    ++target_idx;
  }

CLEANUP:
  fclose(fh);

  parser_free(parser);
  free_scanner(scanner);

  if(nfa_sim) {
    free_nfa_sim(nfa_sim);
  }

  return status;
}
