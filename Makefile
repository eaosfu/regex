SRC      := ./src
OBJDIR   := ./obj
MISCOBJ  := $(OBJDIR)/misc.o
LISTOBJ  := $(OBJDIR)/list.o
TOKENOBJ := $(OBJDIR)/token.o
ALLOBJ    = $(wildcard $(OBJDIR)/*.o)

scanner_test: clean scanner misc token
	$(CC) -o scanner_test ${CFLAGS} ${SRC}/scanner_test.c ${ALLOBJ}


scanner:
	$(CC) -o $(OBJDIR)/scanner.o -c ${CFLAGS} ${SRC}/scanner.c

misc:
	$(CC) -o $(OBJDIR)/misc.o -c ${CFLAGS} ${SRC}/misc.c

nfa: misc
	$(CC) -o $(OBJDIR)/nfa.o -c ${CFLAGS} ${SRC}/nfa.c

list: misc
	$(CC) -o $(OBJDIR)/list.o -c ${CFLAGS} ${SRC}/slist.c

token:
	$(CC) -o $(OBJDIR)/token.o ${CFLAGS} -c ${SRC}/token.c

regex_parser:
	$(CC) -o $(OBJDIR)/regex_parser.o $(CFLAGS) -c ${SRC}/regex_parser.c

recognizer: list token nfa scanner regex_parser misc
	$(CC) ${CFLAGS} ${SRC}/recognizer.c $(ALLOBJ)


test: list
	$(CC) ${CFLAGS} ${SRC}/test.c $(ALLOBJ)

clean:
	rm -f -v obj/* a.out 
