
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

token:
	$(CC) -o $(OBJDIR)/token.o ${CFLAGS} -c token.c

regex_parser:
	$(CC) -o $(OBJDIR)/regex_parser.o $(CFLAGS) -c regex_parser.c

nfa_sim: list token nfa scanner regex_parser misc
	$(CC) ${CFLAGS} nfa_sim.c $(ALLOBJ)


test: list
	$(CC) ${CFLAGS} test.c $(ALLOBJ)

clean:
	rm -f -v obj/* a.out 
