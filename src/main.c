#include "misc.h"
#include "scanner.h"
#include "backtrack_recognizer.h"

#include <getopt.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>

#include <errno.h>

static ino_t stdout_ino;

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


static void
_warn(const char * msg)
{
}


static void
stat_action(const char * pathname, const char * name, List * target_files, List * target_dirs, int rdir)
{
  struct stat sb;

  int name_len = strlen(name);
  if(pathname) {
    // avoid adding '.' and '..' directories
    if(name_len < 3) {
      if(name[0] == '.') {
        if(name_len == 2) {
          if(name[1] == '.') {
            return;
          }
        }
        else {
          return;
        }
      }
    }
  }
  else if(name_len == 2) {
    printf("Ooops: %s\n", name); exit(1);
  }

  char nm[PATH_MAX + NAME_MAX];
  int sz = 0;
  if(pathname) {
    sz = snprintf(NULL, 0, "%s/%s", pathname, name);
    snprintf(nm, ++sz, "%s/%s", pathname, name);
  }
  else {
    sz = snprintf(NULL, 0, "%s", name);
    snprintf(nm, ++sz, "%s", name);
  }

  if(sz < 0) {
    printf("%s: Internal error: ", program_name);
    perror("");
    exit(1);
  }

  errno = 0;
  if(stat(nm, &sb) < 0) {
    fprintf(stderr, "%s: %s: ", program_name, name);
    perror("");
    return;
  }

  if(sb.st_ino == stdout_ino) {
    fprintf(stderr, "%s: input file '%s' is also the output\n", program_name, name);
    return;
  }

  char * fpth = malloc(sz);
  if(fpth == NULL) {
    fatal("Insufficient memory\n");
  }

  if(pathname) {
    snprintf(fpth, sz, "%s/%s", pathname, name);
  }
  else {
    snprintf(fpth, sz, "%s", name);
  }

  switch(sb.st_mode & S_IFMT) {
    case S_IFREG: { // regular file
      //list_append(target_files, (char *)name);
      list_append(target_files, fpth);
    } break;
    case S_IFDIR: { // directory
      if(rdir) {
        //list_append(target_dirs, (char *)name);
        list_append(target_dirs, fpth);
      }
      else {
        fprintf(stderr, "%s: %s: Is a directory\n", program_name, name);
        // FIXME: move this to a 'recognizer_errmsg.h' file
      }
    } break;
/*
    case S_IFCHR: { // char device
      printf("Searching through character devices is currently not supported, skipping %s\n",
        name);
    } break;
    case S_IFSOCK: { // socket
      fprintf(stderr, "Searching sockets is currently not supported, skipping: %s...\n",
        name);
    } break;
    case S_IFIFO: { // FIFO
      fprintf(stderr, "Searching FIFOs is currently not supported, skipping: %s...\n",
        name);
    } break;
    case S_IFLNK: { // symbolic link
      printf("Not sure what to do with symbolic links, skipping %s...\n", name);
    } break;
    case S_IFBLK: { // block device
      printf("Not sure what to do with block devices, skipping %s...\n", name);
    } break;
*/
    default: { // should never reach this case
      printf("%s is not a regular file or directory\n", name);
      free(fpth);
    }
  }
}


static void
process_dir(char * pathname, List * target_files, List * target_dirs)
{
  // if we make it into this function then we are recursively visiting subdirectories
  DIR * dir = NULL;
  struct dirent * de;
  errno = 0;

  if(isatty(STDOUT_FILENO) == 0) {
    struct stat sb;
    if(fstat(STDOUT_FILENO, &sb) < 0) {
      fatal("Internal error\n");
    }
    stdout_ino = sb.st_ino;
  }

  int i = strlen(pathname) - 1;
  while(i > 1) {
    if(pathname[i] == '/') {
      pathname[i] = '\0';
      --i;
      continue;
    }
    break;
  }

  if((dir = opendir(pathname))) {
    while(de = readdir(dir)) {
      stat_action(pathname, de->d_name, target_files, target_dirs, 1);
    }
  }
  else {
    fprintf(stderr, "No such file or directory: %s\n", pathname);
  }
}


static void
process_targets(int argc, char ** argv, int target_idx, List * target_files, List * target_dirs, int rdir)
{
  struct stat sb;
  while(target_idx < argc) {
    if(strlen(argv[target_idx]) == 1 && *(argv[target_idx]) == '-') {
      printf("Searching stdin is currently not supported, skipping..\n");
      ++target_idx;
      continue;
    }
    stat_action(NULL, argv[target_idx], target_files, target_dirs, rdir);
    ++target_idx;
  }
}


int
main(int argc, char ** argv)
{
  int status = 0;
  int rdir   = 0;
  int opt = -1;

  ctrl_flags cfl = SHOW_MATCH_LINE_FLAG;
  Scanner * scanner     = NULL;
  Parser  * parser      = NULL;
  NFASim  * nfa_sim     = NULL;
  FILE    * fh          = NULL;
  const char * filename = NULL;
  char * buffer         = NULL;
  size_t buf_len        = 0;
  unsigned int line_len = 0;
  List * target_files = new_list();
  List * target_dirs = new_list();

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
      case 'r': { rdir = 1;                            } break;
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
  else {
    process_targets(argc, argv, target_idx, target_files, target_dirs, rdir);
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

  scanner  = init_scanner(filename, buffer, buf_len, line_len, &cfl);

  parser = init_parser(scanner, &cfl);

  if(parse_regex(parser) == 0) {
    goto CLEANUP;
  }

  if(fh) {
    fclose(fh);
  }
  

  // if we've made it this far we have all we need to run the recognizer
  NFASimCtrl * nfa_sim_ctrl = new_nfa_sim(parser, scanner, &cfl);
  char * current_target_dir = NULL;
  char * current_target_file = NULL;

PROCESS_TARGET_FILES:
  while((current_target_file = list_shift(target_files))) {
    filename = current_target_file;
    fh = fopen(current_target_file, "r");
    if(fh == NULL) {
      fprintf(stderr, "Unable to open file: %s\n", current_target_file);
      fatal("Unable to open file\n");
    }
    int line = 0;

    while((scanner->line_len = getline(&(scanner->buffer), &(scanner->buf_len), fh)) > 0) {
      reset_scanner(scanner, filename);
      nfa_sim = reset_nfa_sim(nfa_sim_ctrl, ((NFA *)peek(parser->symbol_stack))->parent);
      ++line; // FIXME: scanner should update the line number on it's own
      scanner->line_no = line;
      status += run_nfa(nfa_sim);
    }

    if(nfa_sim_ctrl->match_idx) {
      flush_matches(nfa_sim_ctrl);
    }

    fclose(fh);
    free(current_target_file);
  }

  if((current_target_dir = list_shift(target_dirs))) {
    process_dir(current_target_dir, target_files, target_dirs);
    goto PROCESS_TARGET_FILES;
  }

/*
  while(target_idx < argc) {
    fh = fopen(argv[target_idx], "r");
    if(fh == NULL) {
      fatal("Unable to open file\n");
    }
    filename = argv[target_idx];
    int line = 0;
    while((scanner->line_len = getline(&(scanner->buffer), &(scanner->buf_len), fh)) > 0) {
      reset_scanner(scanner, filename);
      nfa_sim = reset_nfa_sim(nfa_sim_ctrl, ((NFA *)peek(parser->symbol_stack))->parent);
      ++line; // FIXME: scanner should update the line number on it's own
      scanner->line_no = line;
      status += run_nfa(nfa_sim);
    }

    if(nfa_sim_ctrl->match_idx) {
      flush_matches(nfa_sim_ctrl);
    }
    ++target_idx;
  }
*/
CLEANUP:
//  if(fh) fclose(fh);

  list_free(target_files, NULL);
  list_free(target_dirs, NULL);
  parser_free(parser);
  free_scanner(scanner);

  if(nfa_sim) {
    free_nfa_sim(nfa_sim_ctrl);
  }

  return (status == 0);
}
