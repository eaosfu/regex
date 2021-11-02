#include "compile.h"


static void *
compare(void * a, void * b)
{
  if(a == b) {
    return a;
  }
  return NULL;
}


// FIXME: handle equivalent anchors, like '\<\b\<' to be just '\b'
static void
collect_adjacencies_helper(NFA * current, NFA * visiting, int outn, NFA * forbidden, List * adj_intvls_list)
{
  static int recursion = 0;

  if(visiting == forbidden) {
    SET_NFA_CYCLE_FLAG(current);
    return;
  }

  if(visiting->value.type == NFA_EPSILON) {
    SET_NFA_VISITED_FLAG(visiting);
    ++recursion;
    collect_adjacencies_helper(current, visiting->out2, outn, forbidden, adj_intvls_list);
    --recursion;
    CLEAR_NFA_VISITED_FLAG(visiting);
  }
  else {
    // visiting is not an EPSILON
    if(CHECK_NFA_VISITED_FLAG(visiting)) {
      // we've hit this node before
      switch(visiting->value.type) {
        case NFA_TREE:  // fallthrough
        case NFA_SPLIT:
      break;
        default: {
          // if the node is matchable and is not already part of
          // our adjacency list.. add it.
          if(list_search(&(current->reachable), visiting, compare) == NULL) {
            list_append(&(current->reachable), visiting);
          }
        }
      }
    }
    else {
      // we haven't seen this node before
      switch(visiting->value.type) {
        case NFA_TREE: {
          SET_NFA_VISITED_FLAG(visiting);
          NFA * branch = NULL;
          for(int i = 0; i < list_size(visiting->value.branches); ++i) {
            branch = list_get_at(visiting->value.branches, i);
            ++recursion;
            collect_adjacencies_helper(current, branch, outn, forbidden, adj_intvls_list);
            --recursion;
          }
          CLEAR_NFA_VISITED_FLAG(visiting);
        } break;
        case NFA_SPLIT: {
          // Need to add this as a 'reachable' node because when we process an interval
          // in the recognizer we want to avoid changing the state of the 'thread' holding
          // the 'interval' source node while processing the NFA_SPLIT.
          //if(current->value.type == NFA_INTERVAL && (visiting->value.literal != '?')) {
          if(current->value.type == NFA_INTERVAL) {
            // Make sure we don't include this current's starting node in the loop
            collect_adjacencies_helper(current, visiting->out1, outn, current->out1, adj_intvls_list);
            collect_adjacencies_helper(current, visiting->out2, outn, forbidden,  adj_intvls_list);
          }
          else {
            SET_NFA_VISITED_FLAG(visiting);
            ++recursion;
            collect_adjacencies_helper(current, visiting->out1, outn, forbidden, adj_intvls_list);
            collect_adjacencies_helper(current, visiting->out2, outn, forbidden, adj_intvls_list);
            --recursion;
            CLEAR_NFA_VISITED_FLAG(visiting);
          }
        } break;
        default: {
          if(current == visiting) {
            if(recursion == 0) {
              // If the caller passed in current == visiting (i.e. recursion == 0) and
              // current->value.type was some 'matchable' node (i.e. not an NFA_SPLIT,
              // NFA_INTERVAL, etc.) then we still need to explore paths to neighboring
              // nodes.
              ++recursion;
              collect_adjacencies_helper(current, visiting->out2, outn, forbidden, adj_intvls_list);
              --recursion;
              break;
            }
          }
          if(list_search(&(current->reachable), visiting, compare) == NULL) {
            list_append(&(current->reachable), visiting);
          }
        }
      }
    }
  }
  if(visiting->value.type == NFA_ACCEPTING) {
    SET_NFA_ACCEPTS_FLAG(current);
  }
  return;
}


static int
synthesize_pattern(List * patterns, char * root, NFA * nfa, int i)
{
  static int print = 0;
  int ret = 0;
  switch(nfa->value.type) {
    case NFA_LITERAL: {
      root[i] = nfa->value.literal;
      print = 1;
      ret = synthesize_pattern(patterns, root, nfa->out2, ++i);
    } break;
    case NFA_EPSILON: {
      ret = synthesize_pattern(patterns, root, nfa->out2, i);
    } break;
    case NFA_TREE: {
      NFA * tmp = NULL;
      List * l = nfa->value.branches;
      list_set_iterator(l, 0);
      for(tmp = list_get_next(l); tmp != NULL; tmp = list_get_next(l)) {
        ret = synthesize_pattern(patterns, root, tmp, i);
      }
    } break;
    case NFA_CAPTUREGRP_BEGIN:
    case NFA_CAPTUREGRP_END: {
      ret = synthesize_pattern(patterns, root, nfa->out2, i);
    } break;
    case NFA_SPLIT: {
      // don't follow loops (i.e. ignore '+' and '*')
      if(nfa->value.literal == '?') {
        ret = synthesize_pattern(patterns, root, nfa->out1, i);
        ret = synthesize_pattern(patterns, root, nfa->out2, i);
      }
    } break;
    case NFA_ACCEPTING: {
      print = 1;
    } break;
    default: {
      print = 1;
    }
  }

  if(ret != -1) {
    if(print) {
      char * synth_pat = strndup(root, i);
      list_append(patterns, synth_pat);
      print = 0;
    }
    root[i] = 0;
    ret = (i == 0) ? 0 : --i;
  }

  return ret;
}


// THIS SHOULD BE MOVED OUT INTO A 'COMPILE.C' module
// same goes for the 'collect' functions
static void
compute_mpat_tables(Parser * parser, NFA * start)
{
  if(start == NULL) {
    return;
  }

  char * synth_pattern = xmalloc(strlen(parser->scanner->buffer));

  list_set_iterator(&(start->reachable), 0);
  NFA * nfa = NULL;
  while((nfa = list_get_next(&(start->reachable))) != NULL) {
    switch(nfa->value.type) {
      case NFA_LITERAL: {
        synthesize_pattern(parser->synth_patterns, synth_pattern, nfa, 0);
      } break;
      case NFA_ACCEPTING: {
        // do nothing
      } break;
      default: {
        // FIXME:
        // if we fall here we need to decide what to do... for exmaple
        //  we may be able to expand the regex to a point and add it
        //  to our 'patterns' list. but for now make it so the recognizer
        //  simply avoids using the multi-pattern match algorithm.
        goto FREE_PATTERNS_LIST;
      }
    }
  }

FREE_PATTERNS_LIST:
  free(synth_pattern);
  return;
}


void
collect_adjacencies(Parser * parser, NFA * start, int total_collectables)
{
  if(start == NULL) {
    return;
  }

  NFA * current = start;
  NFA * visiting = start;
 
  // branch_stack no longer contains useful data so reuse it as a list
  List * l = parser->branch_stack;;

  // store pairs of adjacent intervals
  //FIXME: currently not being used... still thinking about this
  //       the general idea is that by knowing which intervals connect directly
  //       we can come up with some way of reducing the running time in the recognizer
  //       by avoiding clones that would repeat a sequence of interval counts that
  //       has already appeared while processing a given input position.
  List * adj_intvls_list = new_list(); 

  List * tmp = new_list();

  collect_adjacencies_helper(current, visiting, 0, NULL, NULL);
  list_append(l, current);

  // the 'reachable' list in the start node now contains the first nodes the 
  // recognizer will load... if these nodes are all fixed length strings we
  // can help speed up the recognizer by building a set of tables the recognizer
  // can use to perform a fast multi-pattern search. Doing this search enables
  // the recognizer to quickly jump to skip over positions in the input that
  // would never match the start of the regular expression
  compute_mpat_tables(parser, start);
int i = 0;
//  for(int i = 0; i < list_size(l); ++i) {
  list_for_each(current, l, 0, list_size(l)) {
    if(i > total_collectables) {
      break;
    }
//    current = list_get_at(l, i);
    for(int j = 0; j < list_size(&(current->reachable)); ++j) {
//    list_for_each(visiting, &(current->reachable), 0, list_size(&(current->reachable))) {
      visiting = list_get_at(&(current->reachable),j);
      if(CHECK_NFA_DONE_FLAG(visiting)) {
        continue;
      }
      SET_NFA_DONE_FLAG(visiting);
      if(current != visiting && visiting->value.type != NFA_ACCEPTING) {
        SET_NFA_VISITED_FLAG(visiting);
        if(visiting->value.type == NFA_INTERVAL) {
          collect_adjacencies_helper(visiting, visiting->out1, 0, NULL, NULL);
          visiting->value.split_idx = list_size(&(visiting->reachable));
          list_clear(tmp);
          list_transfer(tmp, &(visiting->reachable)); // stash reachable list
          collect_adjacencies_helper(visiting, visiting->out2, 1, NULL, adj_intvls_list);
          list_transfer(tmp, &(visiting->reachable));
          list_transfer(&(visiting->reachable), tmp); // reconstitute reachable-list for interval node
        }
        else if(visiting->value.type == NFA_SPLIT){
          collect_adjacencies_helper(visiting, visiting->out1, 0, NULL, NULL);
          collect_adjacencies_helper(visiting, visiting->out2, 0, NULL, NULL);
        }
        else {
          collect_adjacencies_helper(visiting, visiting->out2, 0, NULL, NULL);
        }
        CLEAR_NFA_VISITED_FLAG(visiting);
        list_append(l, visiting);
      }
    }
    ++i;
  }

  free(adj_intvls_list);
  free(tmp);
}


void
compile_regex(Parser * parser) {
  // after this call the graph should have several (intermediary) nodes removed.
  collect_adjacencies(parser, (((NFA *)peek(parser->symbol_stack))->parent),
    (parser->total_nfa_ids + parser->interval_count));
}
