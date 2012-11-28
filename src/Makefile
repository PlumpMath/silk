CFLAGS=-Wall -c -g -std=c99
LFLAGS=-g -Wall
LIBS=-l pthread 
SRC=silk_context.c silk_engine.c

samples: $(SRC) *.h
	gcc silk_context.c $(CFLAGS)
	gcc silk_engine.c $(CFLAGS)
	gcc $(LFLAGS) silk_engine.o silk_context.o -o silk $(LIBS)

clean:
	rm -f *.o core silk TAGS
# .PHONY
TAGS:
	etags --output=TAGS *.c *.h

all:
	TAGS
