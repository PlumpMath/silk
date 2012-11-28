CFLAGS=-Wall -c -g -std=c99
CFLAGS_TESTS=-I.
LFLAGS=-g -Wall -Werror
LIBS=-l pthread -l silk
LIB_SRC=silk_context.c silk_engine.c silk_tls.c
LIB_HDR=silk_base.h  silk_context.h  silk.h  silk_sched.h  silk_sched_vanilla.h  silk_tls.h
LIB_OBJ=silk_context.o silk_engine.o silk_tls.o
LIB_SILK=libsilk.a


libsilk.a: $(LIB_SRC) $(LIB_HDR)
	gcc silk_tls.c $(CFLAGS)
	gcc silk_context.c $(CFLAGS)
	gcc silk_engine.c $(CFLAGS)
	ar rcs $(LIB_SILK) $(LIB_OBJ)

run_n.o: run_n.c $(LIB_HDR)
	gcc run_n.c $(CFLAGS) $(CFLAGS_TESTS)

run_n: run_n.o $(LIB_SILK)
	gcc $(LFLAGS) run_n.o -o run_n $(LIBS) -L /home/bename/devel/silk-uthreads

tests: run_n
	echo "building all tests"

clean:
	rm -f *.o core TAGS $(LIB_SILK) run_n

TAGS:
	etags --output=TAGS *.c *.h

.PHONY: all clean tests
all:
	tests TAGS 
