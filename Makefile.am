SRCDIR = src
AM_CFLAGS=-O3

bin_PROGRAMS = recognizer
noinst_LIBRARIES = librecognizer.a

librecognizer_a_SOURCES = $(SRCDIR)/bits.h \
                          $(SRCDIR)/boyer_moore.c \
                          $(SRCDIR)/boyer_moore.h \
                          $(SRCDIR)/cgrps.h \
                          $(SRCDIR)/compile.c \
                          $(SRCDIR)/compile.h \
                          $(SRCDIR)/collations.c \
                          $(SRCDIR)/collations.h \
                          $(SRCDIR)/errmsg.h \
                          $(SRCDIR)/match_record.c \
                          $(SRCDIR)/match_record.h \
                          $(SRCDIR)/misc.c \
                          $(SRCDIR)/misc.h \
                          $(SRCDIR)/mpat.c \
                          $(SRCDIR)/mpat.h \
                          $(SRCDIR)/nfa.c \
                          $(SRCDIR)/nfa.h \
                          $(SRCDIR)/nfa_alloc.c \
                          $(SRCDIR)/nfa_alloc.h \
                          $(SRCDIR)/rbtree.c \
                          $(SRCDIR)/rbtree.h \
                          $(SRCDIR)/recognizer.c \
                          $(SRCDIR)/recognizer.h \
                          $(SRCDIR)/regex_parser.c \
                          $(SRCDIR)/regex_parser.h \
                          $(SRCDIR)/scanner.c \
                          $(SRCDIR)/scanner.h \
                          $(SRCDIR)/slist.c \
                          $(SRCDIR)/slist.h \
                          $(SRCDIR)/stack.h \
                          $(SRCDIR)/token.c \
                          $(SRCDIR)/token.h

TESTS = tests/test.pl

recognizer_SOURCES = $(SRCDIR)/main.c
recognizer_LDADD = librecognizer.a
