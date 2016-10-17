TOP_DIR := $(shell pwd)
OBJ_DIR := ${TOP_DIR}/obj
BIN_DIR:= ${TOP_DIR}/bin
MODULE_DESCRIPTORS:=${TOP_DIR}/build/module_descriptors

.PHONY: all scanner misc nfa slist token regex_parser recognizer

define check_dirs
$(eval $(foreach dir,${BIN_DIR} ${OBJ_DIR}, $(shell if ! [ -d ${dir} ]; then mkdir -v ${dir}; fi;)))
endef

define build_target
$(eval TARGET_TO_UPPER=$(shell echo $(basename $(notdir ${1}))| tr a-z A-Z))
$(info ${${TARGET_TO_UPPER}_TARGET}:;\
  ${CC} -o ${${TARGET_TO_UPPER}_TARGET}\
  ${${CFLAGS}} ${${TARGET_TO_UPPER}_CFLAGS} ${${TARGET_TO_UPPER}_SRC} \
  ${${TARGET_TO_UPPER}_INCLUDE_FLAGS} ${${TARGET_TO_UPPER}_LD_FLAGS} \
  ${${TARGET_TO_UPPER}_LINK_OBJS}
)
$(eval ${${TARGET_TO_UPPER}_TARGET}:;\
  ${CC} -o ${${TARGET_TO_UPPER}_TARGET}\
  ${${CFLAGS}} ${${TARGET_TO_UPPER}_CFLAGS} ${${TARGET_TO_UPPER}_SRC}\
  ${${TARGET_TO_UPPER}_INCLUDE_FLAGS} ${${TARGET_TO_UPPER}_LD_FLAGS}\
  ${${TARGET_TO_UPPER}_LINK_OBJS})
endef

define build_deps
$(foreach d,${1},$(eval $(call build_target,$d)))
endef

define get_deps
$(call check_dirs,)
$(info including ${MODULE_DESCRIPTORS}/${1})
$(eval include ${MODULE_DESCRIPTORS}/${1})
$(eval FINAL_TARGET:=$(shell echo ${1} | tr a-z A-Z))
$(foreach d,${DEPENDS},$(eval include ${MODULE_DESCRIPTORS}/${d}))
$(foreach obj,${DEPENDS},$(eval FINAL_LINK_OBJ += $(addprefix ${OBJ_DIR}/,$(addsuffix .o,${obj}))))
$(eval $(call build_deps,${FINAL_LINK_OBJ}))
endef

define make_goal
$(eval ${1}: ${FINAL_LINK_OBJ};\
	${CC} -o ${${FINAL_TARGET}_TARGET}\
  ${${CFLAGS}} ${${FINAL_TARGET}_CFLAGS} ${${FINAL_TARGET}_SRC}\
  ${${FINAL_TARGET}_INCLUDE_FLAGS} ${${FINAL_TARGET}_LD_FLAGS}\
  ${FINAL_LINK_OBJ})
endef

ifeq ($(MAKECMDGOALS), scanner)
$(eval $(call get_deps,scanner))
$(eval $(call make_goal,scanner))
endif

ifeq ($(MAKECMDGOALS), misc)
$(eval $(call get_deps,misc))
$(eval $(call make_goal,misc))
endif

ifeq ($(MAKECMDGOALS), nfa)
$(eval $(call get_deps,nfa))
$(eval $(call make_goal,nfa))
endif

ifeq ($(MAKECMDGOALS), slist)
$(eval $(call get_deps,slist))
$(eval $(call make_goal,slist))
endif

ifeq ($(MAKECMDGOALS), token)
$(eval $(call get_deps,token))
$(eval $(call make_goal,token))
endif

ifeq ($(MAKECMDGOALS), regex_parser)
$(eval $(call get_deps,regex_parser))
$(eval $(call make_goal,regex_parser))
endif

ifeq ($(MAKECMDGOALS),recognizer)
$(eval $(call get_deps,recognizer))
$(eval $(call make_goal,recognizer))
endif

clean:
	rm -f ${BIN_DIR}/* ${OBJ_DIR}/* 
