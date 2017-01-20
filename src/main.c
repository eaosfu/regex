#include <getopt.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

#include "misc.h"
#include "scanner.h"
#include "backtrack_recognizer.h"

// If you name your program with anywhere near this many bytes...
// seek help.
char program_name[120];

static const char short_options [] = {"Flf:ghiqv"};

static struct option const long_options[] = {
  {"help"           , no_argument      , NULL, 'h'},
  {"global_match"   , no_argument      , NULL, 'g'},
  {"pattern-file"   , required_argument, NULL, 'f'},
  {"show-file-name" , no_argument      , NULL, 'F'}, // not implemented
  {"invert-match"   , no_argument      , NULL, 'v'}, // not implemented
  {"show-match-line", no_argument      , NULL, 'l'}, // not implemented
  {"quiet"          , no_argument      , NULL, 'q'}, // not implemented
  {"silent"         , no_argument      , NULL, 'q'}, // not implemented
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
"  -h, --help               display this help message\n"
"  -g, --global-match       find all matches on the input line\n"
"                           by default only the first match stops the search\n"
"  -f, --pattern-file       read regex pattern from a file\n"
"  -F, --show-file-name     display line number for the match, starts at 1\n"
"  -v, --invert-match       display lines where no matches are found\n"
"  -l, --show-match-line    display line where matches are found\n"
"  -q, --quiet, --silent    suppress all output to stdout\n"
"  -i, --ignore-case        treat all input, including pattern, as lowercase\n"
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
  program_name[len - 1] = '\0';
}


int
main(int argc, char ** argv)
{
  int status = 0;
  int opt = -1;
  int read_pattern_file = 0;

  ctrl_flags cfl;
  Scanner * scanner = NULL;
  Parser  * parser  = NULL;
  NFASim  * nfa_sim = NULL;
  FILE    * fh    = NULL;

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
      case 'L': { SET_SHOW_LINE_FLAG(&cfl);            } break;
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

  scanner  = init_scanner(buffer, buf_len, line_len, &cfl);

  if(scanner->line_len < 0) {
    fatal("UNABLE READ REGEX FILE\n");
  }

  parser = init_parser(scanner, &cfl);

  int run = parse_regex(parser);

  if(fh) {
    fclose(fh);
  }
  

  // if we've made it this far we have all we need to run the recognizer
  while(target_idx < argc) {
    fh = fopen(argv[target_idx], "r");
    if(fh == NULL) {
      fatal("Unable to open file\n");
    }
    int line = 0;
    nfa_sim = new_nfa_sim(parser, scanner, &cfl);
    NFASimCtrl * thread_ctrl = nfa_sim->ctrl;
    while((scanner->line_len = getline(&scanner->buffer, &scanner->buf_len, fh)) > 0) {
      reset_scanner(scanner);
      reset_nfa_sim(nfa_sim, ((NFA *)peek(parser->symbol_stack))->parent);
      run_nfa(nfa_sim);
      nfa_sim = list_shift(thread_ctrl->thread_pool);
      nfa_sim->prev_thread = nfa_sim->next_thread = nfa_sim;
    }

    if(thread_ctrl->match_idx) {
      printf("%s", thread_ctrl->matches);
    }
    ++target_idx;
  }
  fclose(fh);

  parser_free(parser);
  free_scanner(scanner);

  if(nfa_sim) {
    free_nfa_sim(nfa_sim);
  }

  return status;
}