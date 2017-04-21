TOP_DIR := $(shell pwd)
OBJ_DIR := ${TOP_DIR}/obj
BIN_DIR := ${TOP_DIR}/bin
TEST_DRIVER_DIR := .
DEPENDS +=

PERL := /usr/bin/perl
PERL_TEST := ./tests//test.pl
BASH := /bin/bash
RUN_SLIST_TEST := ./bin/test_slist

ifdef MEMCHECK
$(eval RUN_MEMCHECK =--memcheck)
endif

ifdef VERBOSE
$(eval TEST_VERBOSE = --verbose)
endif

MODULE_DESCRIPTORS:=${TOP_DIR}/build/module_descriptors

test_targets    := test_slist test_all wrapper_funcs test_regex regex_alloc
product_targets := scanner misc nfa slist token regex_parser recognizer rbtree mpat collations all

.PHONY: ${product_targets} ${test_targets}

define check_dirs
$(foreach dir,${BIN_DIR} ${OBJ_DIR},
  $(shell if ! [ -d ${dir} ]; then mkdir -v ${dir}; fi;))
endef

define build_dep_target
$(eval TO_UPPER=$(shell echo $(basename $(notdir ${1}))| tr a-z A-Z))
  ifneq (${${TO_UPPER}_TARGET}, $(filter ${${TO_UPPER}_TARGET}, ${generated_recipes}))
    ${${TO_UPPER}_TARGET}:;\
      ${CC} -o ${${TO_UPPER}_TARGET}\
      ${CFLAGS}\
      ${DFLAGS}\
      ${LFLAGS}\
      ${${TO_UPPER}_CFLAGS}\
      ${${TO_UPPER}_DFLAGS}\
      ${${TO_UPPER}_LD_FLAGS}\
      ${${TO_UPPER}_INCLUDE_FLAGS}\
      ${${TO_UPPER}_SRC}\
      ${${TO_UPPER}_LINK_OBJS}
    $(eval generated_recipes += ${${TO_UPPER}_TARGET})
  endif
endef

define make_deps
  $(call check_dirs,)
  $(eval include ${MODULE_DESCRIPTORS}/${1})
  $(eval FINAL_TARGET:=$(shell echo ${1} | tr a-z A-Z))
  $(foreach d,${DEPENDS},$(eval include ${MODULE_DESCRIPTORS}/${d}))
  $(foreach obj,${DEPENDS},
    $(eval FINAL_LINK_OBJ += $(addprefix ${OBJ_DIR}/,$(addsuffix .o,${obj}))))
  $(foreach d,${FINAL_LINK_OBJ},$(eval $(call build_dep_target,$d)))
endef

define make_goal
  $(eval include ${MODULE_DESCRIPTORS}/${1})
  $(eval TO_UPPER=$(shell echo $(basename $(notdir ${1}))| tr a-z A-Z))
  ifneq (${1}, $(filter ${1}, ${generated_recipes}))
    $(eval ${1}: ${FINAL_LINK_OBJ};\
      ${CC} -o ${${FINAL_TARGET}_TARGET}\
      ${DFLAGS}\
      ${LFLAGS}\
      ${CFLAGS}\
      ${${FINAL_TARGET}_CFLAGS}\
      ${${FINAL_TARGET}_DFLAGS}\
      ${${FINAL_TARGET}_LD_FLAGS}\
      ${${FINAL_TARGET}_INCLUDE_FLAGS}\
      ${${FINAL_TARGET}_SRC}\
      ${FINAL_LINK_OBJ}\
      ${${TO_UPPER}_LINK_OBJS})
    $(eval generated_recipes += ${1})
    $(eval FINAL_LINK_OBJ:=)
    $(eval DEPENDS:=)
  endif
endef

define make_match_record
  $(call make_deps,match_record)
  $(call make_goal,match_record)
endef

define make_booyer_moore
  $(call make_deps,booyer_moore)
  $(call make_goal,booyer_moore)
endef

define make_nfa_alloc
  $(call make_deps,nfa_alloc)
  $(call make_goal,nfa_alloc)
endef

define make_wrapper_funcs
  $(call make_deps,wrapper_funcs)
  $(call make_goal,wrapper_funcs)
endef

define make_collations
  $(call make_deps,collations)
  $(call make_goal,collations)
endef

define make_scanner
  $(call make_deps,scanner)
  $(call make_goal,scanner)
endef

define make_misc
  $(call make_deps,misc)
  $(call make_goal,misc)
endef

define make_nfa
  $(call make_deps,nfa)
  $(call make_goal,nfa)
endef

define make_slist
  $(call make_deps,slist)
  $(call make_goal,slist)
endef

define make_token
  $(call make_deps,token)
  $(call make_goal,token)
endef

define make_regex_parser
  $(call make_deps,regex_parser)
  $(call make_goal,regex_parser)
endef

#define make_recognizer
#  $(call make_deps,recognizer)
#  $(call make_goal,recognizer)
#endef

define make_mpat
  $(call make_deps,mpat)
  $(call make_goal,mpat)
endef

define make_rbtree
  $(call make_deps,rbtree)
  $(call make_goal,rbtree)
endef

define make_recognizer
  $(call make_deps,recognizer)
  $(call make_goal,recognizer)
endef

define make_regex
  $(call make_deps,regex)
  $(call make_goal,regex)
endef

define make_test_slist
  $(eval CFLAGS +=-g)
  $(call make_deps,test_slist)
  $(call make_goal,test_slist)
  $(shell ${RUN_SLIST_TEST})
endef

define make_regex_alloc
  $(eval $(call make_clean))
  $(eval CFLAGS=-g -rdynamic)
  $(call make_deps,regex_alloc)
  $(call make_goal,regex_alloc)
endef

define make_test_all
  $(eval $(call make_clean))
  $(eval CFLAGS +=-g)
  $(eval CFLAGS +=-Wall)
  $(call make_regex)
  test_all: clean regex
		${PERL} ${PERL_TEST} ${RUN_MEMCHECK} ${TEST_VERBOSE}
endef

define make_all
  $(eval CFLAGS += -O3)
  $(eval CFLAGS += -Wall)
  $(eval $(call make_clean))
  $(call make_regex)
  all: clean regex
endef

define make_clean
clean:
	rm -f ${BIN_DIR}/* ${OBJ_DIR}/* 
endef

$(foreach goal,$(MAKECMDGOALS),$(eval $(call $(addprefix make_,${goal}))))

