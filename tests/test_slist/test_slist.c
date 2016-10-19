#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/slist.h"
#include "../../src/misc.h"

#define NUM_TESTS 11

typedef enum {FAIL = 0, PASS = 1, FATAL = 2} test_status;

typedef enum {
  LIST_PUSH = 0,
  LIST_INSERT_AT,       LIST_APPEND, LIST_SHIFT,
  LIST_REMOVE_AT,       LIST_GET_AT, LIST_CLEAR,
  LIST_RELEASE_TO_POOL, LIST_FREE,   LIST_DEEP_COPY
} test_name;


int  g_issued_warning = 0;
char g_tests_passed[NUM_TESTS] = {0};


// TEST UTILITY FUNCTIONS
#define list_assert(a)  (((a) != 0) ? PASS : FAIL)

void
report_test_status(const char * test_name, const char * msg, test_status status)
{
  char status_name[6] = {0};
  switch(status) { 
    case PASS: strncpy(status_name, "PASS", strlen("PASS"));
    break;
    case FAIL: strncpy(status_name, "FAIL", strlen("FAIL"));
    break;
    case FATAL: strncpy(status_name, "FATAL", strlen("FATAL"));
    break;
    default: // should never hit this case
      printf("Unrecognized test status... exiting with 1\n");
      exit(1);
    break;
  }

  if(msg) {
    fprintf(stderr, "%s: %s: %s\n", test_name, status_name, msg);
  }
  else {
    fprintf(stderr, "%s: %s\n", test_name, status_name);
  }

  if(status == FATAL) {
    exit(1);
  }
}


test_status
check_list_size(const char * test_name, int expected, int actual)
{
  if(!list_assert(expected == actual)) {
    report_test_status(test_name, "Unexpected change in list->size", FAIL);
    return FAIL;
  }
  return PASS;
}


test_status
check_pool_size(const char * test_name, int expected, int actual)
{
  if(!list_assert(expected == actual)) {
    report_test_status(test_name, "Unexpected change in list->pool_size", FAIL);
    return FAIL;
  }
  return PASS;
}


test_status
check_head_address(const char * test_name, ListItem * expected, ListItem * actual)
{
  if(!list_assert(expected == actual)) {
    report_test_status(test_name, "Unexpected change in value pointed to by list->head", FAIL);
    return FAIL;
  }
  return PASS;
}


test_status
check_pool_address(const char * test_name, ListItem * expected, ListItem * actual)
{
  if(!list_assert(expected == actual)) {
    report_test_status(test_name, "Unexpected change in value pointed to by list->pool", FAIL);
    return FAIL;
  }
  return PASS;
}


static inline void
mark_test_passed(test_name test)
{
  if(test >= NUM_TESTS && g_issued_warning == 0) {
    g_issued_warning = 1;
    warn("Tests run exceeds number of tests planned for... "
      "Unable to keep track of test dependencies from this point onward \n");
  }
  else {
    g_tests_passed[test] = 1;
  }
}


int
check_test_passed(test_name test)
{
  int ret = 0;
  if(test < NUM_TESTS) {
    ret = g_tests_passed[test];
  }
  return ret;
}

void
reset_list(List * list, void * data_list[], unsigned int data_size)
{
  const char * test_name = "reste_list";
  if(check_test_passed(LIST_CLEAR) == 0) {
    report_test_status(test_name, "Unable to reset list unless list_clear is knonw to be good", FATAL);
  }
  else {
    list_clear(list);
  }
}

// DONE TEST UTILITY FUNCTIONS



// LIST UNIT TESTS

List *
test_new_list(List * list)
{
  const char * test_name = "test_new_list";
  list = new_list();
  if(list == NULL) {
    report_test_status(test_name, "Failed to create list", FATAL);
  }
  else {
    report_test_status(test_name, NULL, PASS);
  }
  return list;
}

test_status
test_list_push(List * list, void * d)
{
  const char * test_name = "test_list_push";
  int fail_set = 0;
  int list_size = list->size;
  int pool_size = list->pool_size;
  test_status ret_status = FAIL;
  ListItem * old_list_head = list->head;
  ListItem * pool_p = list->pool;

  if(list == NULL) {
    report_test_status(test_name, "This test requires a non-NULL 'list' object as an argument.", FAIL);
  }
  else {
    if(!list_assert(list_push(list, d) == ++list_size)) {
      report_test_status(test_name, "Value returned by list_push does not match expected new list size", FAIL);
      fail_set = 1;
    }
    
    if(check_list_size(test_name, list_size, list->size) == FAIL) {
      fail_set = 1;
    }

    if(list_assert(list->head == NULL)) {
      report_test_status(test_name, "list->head incorrectly set to NULL", FAIL);
      fail_set = 1;
    }
    else {
      if(!list_assert(list->head->data ==  d)) {
        report_test_status(test_name, "list->head->data does not match data pushed", FAIL);
        fail_set = 1;
      }
      if(!list_assert(list->head->next == old_list_head)) {
        report_test_status(test_name, "list->head->next does not point to the previous head", FAIL);
        fail_set = 1;
      }
    }

    if(!list_assert(list->pool == pool_p)) {
      report_test_status(test_name, "Unexpected change of list->pool", FAIL);
      fail_set = 1;
    }

    if(check_pool_size(test_name, list->pool_size, pool_size) == FAIL) {
      fail_set = 1;
    }

    if(!fail_set) {
      report_test_status(test_name, NULL, PASS);
      mark_test_passed(LIST_PUSH);
      ret_status = PASS;
    }
  }

  return ret_status;
}


test_status
test_list_insert_at(List * list, void * d, int idx)
{
  const char * test_name = "test_list_insert_at";
  int fail_set = 0;
  int list_size = list->size;
  int pool_size = list->pool_size;
  test_status ret_status = FAIL;
  test_status node_pointers_match;
  List * unmodified_list = NULL;
  ListItem * old_head_p = list->head;
  ListItem * pool_p = list->pool;

  void * old_head_data = (list->head == NULL)? NULL : list->head->data;

  int expected_idx = idx;

  if(idx < 0) {
    expected_idx = -1;
  }
  else if(idx > list->size) {
    expected_idx = list->size;
  }

//  printf("\tidx: %d\n", idx);
  
  if(list == NULL) {
    report_test_status(test_name, "This test requires a non-NULL 'list' object as an argument.", FAIL);
  }
  else {
    if(!list_assert(list_insert_at(list, d, idx) == expected_idx)) {
      report_test_status(test_name, "Expected insert index does not match actual insert index", FAIL);
    }

    if(idx < 0) {
      if(!list_assert(list->head == old_head_p)) {
        report_test_status(test_name, "Invalid modification of list->head while"
                                      " attempting to insert at idx < 0", FAIL);
        fail_set = 1;
      }
    }
    else if(idx == 0) {
      if(old_head_p == NULL) {
        if(list_assert(list->head == NULL)) {
          report_test_status(test_name, "Failed to update list->head:null while attempting to "
                                        "insert at idx == 0", FAIL);
          fail_set = 1;
        }
        else if(!list_assert(list->head->data == d)) {
          report_test_status(test_name, "Value pointed to by list->head->data doesn't match "
                                        "argument data pointer", FAIL);
          fail_set = 1;
        }
      }
      else {
        if(list_assert(list->head == NULL)) {
          report_test_status(test_name, "Failed to update list->head:non-null while attempting to "
                                        "insert at idx == 0", FAIL);
          fail_set = 1;
        }
        else if(!list_assert(list->head->data == d)) {
          report_test_status(test_name, "Value pointed to by list->head->data doesn't match "
                                        "argument data pointer", FAIL);
          fail_set = 1;
        }
      }
    }
    else {
      if(old_head_p == NULL) {
        if(list_assert(list->head == NULL)) {
          report_test_status(test_name, "Failed to update list->head:null while attempting to "
                                        "insert at idx > 0", FAIL);
          fail_set = 1;
        }
        else if(!list_assert(list->head->data == d)) {
          report_test_status(test_name, "", FAIL);
          fail_set = 1;
        }
      }
      else {
        if(list_assert(list->head == NULL)) {
          report_test_status(test_name, "Failed to update list->head:non-null while attempting to "
                                        "insert at idx > 0", FAIL);
          fail_set = 1;
        }
        else if(!list_assert(list->head->data == old_head_data)) {
          report_test_status(test_name, "Value pointed to by list->head->data differs from its "
                                        "original value", FAIL);
          fail_set = 1;
        }
      }
    }

    if(idx >= 0) {
      list_size++;
    }

    if(check_list_size(test_name, list->size, list_size) == FAIL) {
      fail_set = 1;
    }

    // pool and pool_size should remain unaffected by an insert operation
    if(!list_assert(list->pool == pool_p)) {
      report_test_status(test_name, "", FAIL);
      fail_set = 1;
    }

    if(check_pool_size(test_name, list->pool_size, pool_size) == FAIL) {
      fail_set = 1;
    }

    if(!fail_set) {
      report_test_status(test_name, NULL, PASS);
      mark_test_passed(LIST_INSERT_AT);
      ret_status = PASS;
    }

  }

  return ret_status;
}


test_status
test_list_append(List * list, void * data_list[], int data_list_size)
{
  const char * test_name = "test_list_append";
  test_status ret_status = FAIL;
  ListItem * list_end = NULL;
  int expected_list_size, expected_pool_size;
  int expected_idx;
  int fail_set = 0;

  // TODO check for test_dependencies
  // DEPENDS ON:
  //   - new_list()
  if(list == NULL) {
    report_test_status(test_name, "This test requires a non-NULL 'list' object as an argument.", FAIL);
  }
  else if(list->pool != NULL || list->pool_size != 0) {
    // ensure pool is empty so that the list->pool and pool_size don't change
    report_test_status(test_name, "'This test requires no allocations be made from the pool."
                                  " The provided list has a non empty pool", FAIL);
  }
  else if(data_list_size == 0) {
    report_test_status(test_name, "No test data to append to list", FAIL);
  }
  else {
    expected_list_size = list->size;
    expected_pool_size = list->pool_size;
    list_end = list->head;

    while(list_end && list_end->next && (list_end = list_end->next));

    expected_idx = list->size - 1;

    int ret_val = 0;

    for(int i = 0; i < data_list_size; ++i) {
      if(!list_assert((ret_val = list_append(list, data_list[i])) == expected_idx++)) {
        report_test_status(test_name, "Value returned by 'list_append' doesn't match expected index", FAIL);
        fail_set = 1;
        break;
      }

      if(list_assert(list_end->next == NULL)) {
        report_test_status(test_name, "Failed to append new item to end of the list", FAIL);
        fail_set = 1;
        break;
      }
      else {
        if(!list_assert(list_end->next->data == data_list[i])) {
          report_test_status(test_name, "Address of data appended is incorrect", FAIL);
          fail_set = 1;
          break;
        }
      }

      if(check_list_size(test_name, list->size, ++expected_list_size) == FAIL) {
        fail_set = 1;
        break;
      }

      // Pool size should not change 
      if(check_pool_size(test_name, list->pool_size, expected_pool_size) == FAIL) {
        fail_set = 1;
        break;
      }

      if(!list_assert(list->pool == NULL)) {
        report_test_status(test_name, "Unexpected change in value pointed to by list->pool", FAIL);
      }

      list_end = list_end->next;

      if(!fail_set) {
        report_test_status(test_name, NULL, PASS);
        mark_test_passed(LIST_APPEND);
        ret_status = PASS;
      }
    }

    if(!list_assert(list_end->next == NULL)) {
      report_test_status(test_name, "The 'next' pointer at end of list does not point to 'NULL'", FAIL);
      fail_set = 1;
    }


  }

  return ret_status;
}


test_status
test_list_shift(List * list)
{
  const char * test_name = "test_list_shift";
  void * list_head = list->head->data;
  test_status ret_status = FAIL;

  if(list_assert(list_shift(list) == list_head) == FAIL) {
    report_test_status(test_name, "Data returned by list_shift() does not match expected data", FAIL);
  }
  else {
    report_test_status(test_name, NULL, PASS);
    mark_test_passed(LIST_SHIFT);
    ret_status = PASS;
  }

  return ret_status;
}


test_status
test_list_remove_at(List * list)
{
  const char * test_name = "test_list_remove_at";
  test_status ret_status = FAIL;
  int fail_set = 0;
  void * expected_data;

  if(list == NULL) {
    report_test_status(test_name, "This test requires a non-NULL 'list' object as an argument.", FAIL);
  }
  else {
    if((check_test_passed(LIST_INSERT_AT)) == 0) {
      report_test_status(test_name,
        "list_insert_at be tested before this test can run", FATAL);
    }

    if(check_test_passed(LIST_GET_AT) == 0) {
      report_test_status(test_name,
        "list_get_at must be tested before this test can run", FATAL);
    }

    // idx == - 1 .. should return NULL
    if(list_assert(list_remove_at(list, -1) == NULL) == FAIL) {
      report_test_status(test_name, "Non NULL value returned at index -1", FAIL);
      fail_set = 1;
    }

    // idx == 0
    expected_data = list_get_at(list, 0);
    if(list_assert(list_remove_at(list, 0) == expected_data) == FAIL) {
      report_test_status(test_name, "Data returned at idx 0 does not match data at list head", FAIL);
      fail_set = 1;
    }

    // idx == 1
    expected_data = (list->size >= 2) ? list_get_at(list, 1) : NULL;
    if(list_assert(list_remove_at(list, 1) == expected_data) == FAIL) {
      report_test_status(test_name, "Data returned at idx 0 does not match data at idx 1", FAIL);
      fail_set = 1;
    }

    // idx > list_size
    if(list_assert(list_remove_at(list, list->size + 1) == NULL) == FAIL) {
      report_test_status(test_name, "Data returned at idx 0 does not match data at idx = list->size + 1", FAIL);
      fail_set = 1;
    }

    // idx = list_size
    if(list_assert(list_remove_at(list, list->size) == NULL) == FAIL) {
      report_test_status(test_name, "Data returned at idx 0 does not match data at idx = list->size", FAIL);
      fail_set = 1;
    }

    // idx = list_size - 1
    expected_data = (list->size >= 1) ? list_get_at(list, list->size - 1) : NULL;
    if(list_assert(list_remove_at(list, list->size - 1) == expected_data) == FAIL) {
      report_test_status(test_name, "Data returned at idx 0 does not match data at idx = list->size + 1", FAIL);
      fail_set = 1;
    }

    // idx = list_size - 2
    expected_data = (list->size >= 2) ? list_get_at(list, list->size - 2) : NULL;
    if(list_assert(list_remove_at(list, list->size - 2) == expected_data) == FAIL) {
      report_test_status(test_name, "Data returned at idx 0 does not match data at idx = list->size + 1", FAIL);
      fail_set = 1;
    }

    if(fail_set == 0) {
      report_test_status(test_name, NULL, PASS);
      mark_test_passed(LIST_REMOVE_AT);
      ret_status = PASS;
    }
  }

  return ret_status;
}


test_status
test_list_get_at(List * list, void * data, unsigned int data_idx)
{
  const char * test_name = "test_list_get_at";
  test_status ret_status = FAIL;
  int fail_set = 0;
  void * expected_data;

  if(list == NULL) {
    report_test_status(test_name, "This test requires a non-NULL 'list' object as an argument.", FAIL);
  }
  else {
    if(list_assert(data_idx >= list->size) == FAIL) {
    }

    if(fail_set == 0) {
      report_test_status(test_name, NULL, PASS);
      mark_test_passed(LIST_GET_AT);
      ret_status = PASS;
    }
  }

  return ret_status;
}


test_status
test_list_clear(List * list, void * data_list[], unsigned int list_size)
{
  const char * test_name = "test_list_clear";
  test_status ret_status = FAIL;
  int fail_set = 0;
  void * expected_data;

  if(list == NULL) {
    report_test_status(test_name, "This test requires a non-NULL 'list' object as an argument.", FAIL);
  }
  else {
    unsigned int list_size = list->size;
    unsigned int pool_size = list->pool_size;
    unsigned int expected_pool_size = pool_size + list_size;
    unsigned int max_pool_idx = 0;
    
    list_clear(list);

    if(list_assert(list->pool_size == expected_pool_size) == FAIL) {
      report_test_status(test_name, "List pool_size after call to list_clear differs from expected size", FAIL);
      fail_set = 1;
    }

    if(list_assert(list->head == NULL) == FAIL) {
      report_test_status(test_name, "List clear failed to empty list", FAIL);
      fail_set = 1;
    }

    ListItem * pool_ptr = list->pool;
    unsigned int iter = 0;

    // make sure we can actually reach all of the nodes in the pool
    while(pool_ptr && pool_ptr->next && (pool_ptr = pool_ptr->next) && ++iter);

    max_pool_idx = (expected_pool_size -= (list_size > 0) ? 1 : (pool_size > 0) ? 1 : 0);
    if(list_assert(max_pool_idx == iter) == FAIL) {
      report_test_status(test_name, "Number of elemnts in list->pool is inconsitent with list->pool_size", FAIL);
      fail_set = 1;
    }

    if(fail_set == 0) {
      report_test_status(test_name, NULL, PASS);
      //mark_test_passed(LIST_CLEAR);
      ret_status = PASS;
    }
  }

  if(fail_set == 0)

  return ret_status;
}


test_status
test_list_release_to_pool(List * list)
{
}


test_status
test_list_free(List * list)
{
}


test_status
test_list_deep_copy()
{
}


// DONE LIST UNIT TESTS

int
main(void)
{

  //List * list = new_list();
  struct { int a; } d1 = { .a = 10 };
  struct { int a; } d2 = { .a = 20 };
  struct { int a; } d3 = { .a = 20 };
  struct { int a; } d4 = { .a = 50 };
/*
  // SHOULD NOT ANYTHING ANYTHING
  list_assert(list_remove_at(list, 10), NULL);

  // REMOVE THE HEAD
  list_assert(list_remove_at(list, 0), (void *)&d2);
  
  // REMOVE MIDDLE ELEMENT
  // void * rm = list_remove_at(list, 1);

  // REMOVE END ELEMENT
  // void * rm = list_remove_at(list, 2);

  // REMOVE THE HEAD
// list_assert(list_shift(list), );
  list_assert(list_get_at(list, -1), NULL);
  list_assert(list_get_at(list, -4), NULL);
// list_assert(list_get_at(list, 0), );
//  list_assert(list_get_at(list, list->size - 1), );
  list_assert(list_get_at(list, 20), NULL);

  list_clear(list);

  list_assert(list->head, NULL);
  list_assert(list->size, 0);

// list_assert(list->pool, );



  list_free(list, NULL);
//  list_assert();
*/
  List * list = NULL;
  list = test_new_list(list);


// INSERT AT
  test_list_insert_at(list, (void *)&d1, 0);
  test_list_insert_at(list, (void *)&d1, 10);
  test_list_insert_at(list, (void *)&d2, 10);
  test_list_insert_at(list, (void *)&d2, 0);
  test_list_insert_at(list, (void *)&d3, 2);

  //test_list_free(list);

// APPEND
  void * append_list[4] = {(void *)&d4, (void *)&d3, (void *)&d2, (void *)&d1};
  test_list_append(list, append_list, 4);

// PUSH
  test_list_push(list, (void *)22);

// SHIFT
  test_list_shift(list);

// CLEAR
  test_list_clear(list, append_list, 4);


// MAKE SURE LIST IS AS WHEN WE STARTED
  reset_list(list, append_list, 4);


// GET_AT
  test_list_get_at(list, append_list[2], 2);

// REMOVE AT
  test_list_remove_at(list);


// MAKE SURE LIST IS AS WHEN WE STARTED
  reset_list(list, append_list, 4);

}
