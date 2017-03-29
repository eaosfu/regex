#include "misc.h"
#include "scanner.h"
#include "backtrack_recognizer.h"

#include <libgen.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <fts.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <string.h>


static ino_t stdout_ino;
static int fts_options = FTS_COMFOLLOW|FTS_LOGICAL; 
static int recurse_dirs = 0;
static int suppress_errors = 0;
static const char short_options [] = {"Ff:ghinoqrsv"}; // missing E and T options


static struct option const long_options[] = {
  {"help"            , no_argument      , NULL, 'h'}, 
  {"global_match"    , no_argument      , NULL, 'g'},
  {"pattern-file"    , required_argument, NULL, 'f'},
  {"show-file-name"  , no_argument      , NULL, 'F'},
  {"invert-match"    , no_argument      , NULL, 'v'},
  {"show-line-number", no_argument      , NULL, 'n'},
  {"show-match-only" , no_argument      , NULL, 'o'},
  {"quiet"           , no_argument      , NULL, 'q'},
  {"silent"          , no_argument      , NULL, 'q'},
  {"no-messages"     , no_argument      , NULL, 's'},
  {"ignore-case"     , no_argument      , NULL, 'i'},
//  {"exclude"         , no_argument      , NULL, 'E'},  // not implemented
//  {"parallel"        , no_argument      , NULL, 'T'},  // not implemented
  {"recursive"       , no_argument      , NULL, 'r'},
  {0                 , 0                , 0   ,  0 }
};


static void
get_stdout_ino()
{
  if(isatty(STDOUT_FILENO) == 0) {
    struct stat sb;
    if(fstat(STDOUT_FILENO, &sb) < 0) {
      fatal("Internal error\n");
    }
    stdout_ino = sb.st_ino;
  }
}


static void
process_file(int cwd_fd, FTSENT * ftsent, Scanner * scanner, Parser * parser, NFASimCtrl * nfa_sim_ctrl, ctrl_flags * cfl, int from_command_line)
{
  int fd;
  int openat_opts = O_RDONLY|O_NOCTTY;
  openat_opts |= (from_command_line) ? 0 : O_NOFOLLOW;

  fd = openat(cwd_fd, ftsent->fts_accpath, openat_opts);
  if(fd == -1) {
    warn("%s", ftsent->fts_accpath);
  }
  else {
    if(ftsent->fts_statp->st_ino == stdout_ino) {
      fprintf(stderr, "%s: input file '%s' is also the output\n", program_name, ftsent->fts_accpath);
    }
    else {
      // do more file processing here
      // this is where we want to call our matcher
      int status = 0;
      int line = 0;
      FILE * fh = NULL;
      NFASim  * nfa_sim     = NULL;

      if((fh = fdopen(fd, "r")) == NULL) {
        err(EXIT_FAILURE, "unable to open file: %s", ftsent->fts_accpath);
      }

      while((scanner->line_len = getline(&(scanner->buffer), &(scanner->buf_len), fh)) > 0) {
        reset_scanner(scanner, ftsent->fts_accpath);
        nfa_sim = reset_nfa_sim(nfa_sim_ctrl, ((NFA *)peek(parser->symbol_stack))->parent);
        ++line; // FIXME: scanner should update the line number on it's own
        scanner->line_no = line;
        status += run_nfa(nfa_sim);
        if(status != 0 && ((*cfl) & SILENT_MATCH_FLAG)) {
          // no need to keep searching if we've found a match
          break;
        }
      }
      if(nfa_sim_ctrl->match_idx) {
        flush_matches(nfa_sim_ctrl);
      }
      fclose(fh);
    }
  }
  close(fd);
  return;
}


// FIXME: remove this when 'stdin' processing is finally supported
static inline int __attribute__((always_inline))
is_stdin(char * argv)
{
  if(strlen(argv) == 1 && *argv == '-')  {
    if(suppress_errors == 0) {
      fprintf(stderr, "%s: reading from 'stding' not yet supported, skipping...\n",
              program_name);
    }
    return 1;
  }
  return 0;
}


void
handle_search_targets(int cwd_fd, int idx, int argc, char ** argv, Scanner * scanner, Parser * parser, NFASimCtrl * nfa_sim, ctrl_flags * cfl)
{
  FTS * fts;
  for(; idx < argc; ++idx) {

    if(is_stdin(argv[idx]))  {
      continue;
    }

    char * fts_arg[2] = { argv[idx], 0};

    if((fts = fts_open(fts_arg, fts_options, NULL)) != NULL) {
      FTSENT * ftsent;
      while((ftsent = fts_read(fts)) != NULL) {
        switch(ftsent->fts_info) {
          case FTS_SL: {
            fprintf(stderr, "THIS IS A LINK -- %s\n", ftsent->fts_accpath);
          } break;
          case FTS_NSOK:  // fall through
          case FTS_DEFAULT: {
            // this isn't a dir or a regular file...
            fprintf(stderr, "%s: reading from devices not yet supported: skipping '%s'...\n",
                    program_name, ftsent->fts_accpath);
            // if we make this far we can handle this type of file
          } break;
          case FTS_SLNONE:
          case FTS_F: {
            // FIXME - programmatically set the last argument to 'process_files' so we can
            //         let the user decide whether or not to read symbolic links not listed
            //         on the command line.
            process_file(cwd_fd, ftsent, scanner, parser, nfa_sim, cfl, 0);
          } break;
          case FTS_NS:  // fall through
          case FTS_ERR: // fall through
          case FTS_DNR: {
            if(suppress_errors == 0) {
              warn("%s", ftsent->fts_accpath);
            }
          } break;
          case FTS_D: {
            // do we want to process the directories recursively?
            // if not give a warning
            if(recurse_dirs == 0) {
              if(suppress_errors == 0) {
                fprintf(stderr, "%s: %s is a directory\n", program_name, ftsent->fts_accpath);
              }
              fts_set(fts, ftsent, FTS_SKIP);
              continue;
            }
            // do nothing -- fts_open will eventually drop us into this directory
          } break;
          case FTS_DP: {
            // do nothing -- fts_open only visits directories once
            continue;
          } break;
          case FTS_DC: {
            if(suppress_errors == 0) {
              fprintf(stderr, "%s: %s: recursive directory loop\n",
                      program_name, ftsent->fts_accpath);
            }
          } break;
          default: {
            fprintf(stderr, "WHAT?: %s\n", ftsent->fts_accpath);
          }
        }
        // reset our errno in preparation for next iteration
        errno = 0;
      }
      if(errno != 0) {
        // possible error from fts_read
        err(EXIT_FAILURE, "HERE");
      }
    }
    else if(errno != 0) {
      // this is probably a serious error... report and exit
      err(EXIT_FAILURE, "HERE2");
    }
    // reset our errno in preparation for next iteration
    errno = 0;
  }

  fts_close(fts);
}


static void
print_description(void)
{
  printf("%s: Use regular expressions to search ""for <pattern> in <files>\n\n",
    program_name);
}


static void
print_usage(int exit_code)
{
  printf("usage: %s [options] '<regex>'  file [file]...\n", program_name);
  printf("       %s [options] -f <pattern_file> file [file]...\n", program_name);
//  printf("       %s [options] -f <pattern_file> -E='exclude1 exclude2...' file [file]...\n", program_name);
  printf("\n");
  printf("options:\n"
//"  -T, --threaded           when searching multiple files/directories do so in parallel\n"
//"  -E, --exclude            list of files/directories to exclude from search\n"
"  -F, --show-file-name     display filename where match was found\n"
"  -f, --pattern-file       use first line from file as the regex pattern\n"
"  -g, --global-match       find all matches on the input line\n"
"                           by default the first match stops the search\n"
"  -h, --help               display this help message\n"
"  -i, --ignore-case        treat all input, including pattern, as lowercase\n"
"  -n, --show-match-line    display line number for the match, starts at 1\n"
"  -o, --show-match-only    only display matching string\n"
"  -q, --quiet, --silent    suppress all output to stdout\n"
"  -r, --recusrive          suppress all output to stdout\n"
"  -s, --no-messages        suppress messges about nonexistent/unreadable input\n"
"  -v, --invert-match       display lines where no matches are found\n"
"\n"
"Exit status is 0 if a match is found anywhere, 1 otherwise.\n");
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

  ctrl_flags cfl = SHOW_MATCH_LINE_FLAG;
  Scanner * scanner     = NULL;
  Parser  * parser      = NULL;
  FILE    * fh          = NULL;
  const char * filename = NULL;
  char * buffer         = NULL;
  size_t buf_len        = 0;
  unsigned int line_len = 0;
//  List * target_files = new_list();
//  List * target_dirs = new_list();

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
      case 'v': { CLEAR_SHOW_MATCH_LINE_FLAG(&cfl);
                  SET_INVERT_MATCH_FLAG(&cfl);         } break;
      case 'F': { SET_SHOW_FILE_NAME_FLAG(&cfl);       } break;
      case 'n': { SET_SHOW_LINENO_FLAG(&cfl);          } break;
      case 'o': { CLEAR_SHOW_MATCH_LINE_FLAG(&cfl);    } break;
      case 'r': { recurse_dirs = 1;
                  SET_SHOW_FILE_NAME_FLAG(&cfl);       } break;
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
    exit_msg_usage("ERROR: No search file(s) provided", EXIT_FAILURE);
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
    fatal("Unable to read regex file\n");
  }

  if((scanner = init_scanner(filename, buffer, buf_len, line_len, &cfl)) == NULL) {
    goto CLEANUP_SCANNER;
  }

  if((parser = init_parser(scanner, &cfl)) == NULL) {
    goto CLEANUP_PARSER;
  }

  if(parse_regex(parser) == 0) {
    goto CLEANUP_ALL;
  }

  if(fh) {
    fclose(fh);
  }

  get_stdout_ino();

  // if we've made it this far we have all we need to run the recognizer
  NFASimCtrl * nfa_sim_ctrl = new_nfa_sim(parser, scanner, &cfl);

  handle_search_targets(AT_FDCWD, target_idx, argc, argv, scanner, parser, nfa_sim_ctrl, &cfl);

CLEANUP_ALL:
  if(nfa_sim_ctrl) {
    free_nfa_sim(nfa_sim_ctrl);
  }

CLEANUP_PARSER:
  parser_free(parser);

CLEANUP_SCANNER:
  free_scanner(scanner);

  return (status == 0);
}
