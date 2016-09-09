
OBJDIR   := ./obj
MISCOBJ  := $(OBJDIR)/misc.o
LISTOBJ  := $(OBJDIR)/list.o
TOKENOBJ := $(OBJDIR)/token.o
ALLOBJ    = $(wildcard $(OBJDIR)/*.o)

scanner_test: clean scanner misc token
	$(CC) -o scanner_test ${CFLAGS} scanner_test.c ${ALLOBJ}


scanner:
	$(CC) -o $(OBJDIR)/scanner.o -c ${CFLAGS} scanner.c

misc:
	$(CC) -o $(OBJDIR)/misc.o -c ${CFLAGS} misc.c

nfa: misc
	$(CC) -o $(OBJDIR)/nfa.o -c ${CFLAGS} nfa.c

list: misc
	$(CC) -o $(OBJDIR)/list.o -c ${CFLAGS} slist.c

token: misc
	$(CC) -o $(OBJDIR)/token.o ${CFLAGS} -c token.c

charclass: misc
	$(CC) $(OBJDIR)/charclass.o $(CFLAGS) -c charclass.c

regex_parser: list token nfa scanner
	$(CC) ${CFLAGS} regex_parser.c $(ALLOBJ)


test: list
	$(CC) ${CFLAGS} test.c $(ALLOBJ)

clean:
	rm -f -v obj/* a.out 
