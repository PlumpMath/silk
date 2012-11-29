CFLAGS=-Wall  -Werror -c -g -std=c99 -Ofast
CFLAGS_TESTS=-I.
LFLAGS=-g -Wall -Ofast
LIBS=-l pthread -l silk
LIB_SRC=silk_context.c silk_engine.c silk_tls.c
LIB_HDR=config.h silk_base.h silk_context.h silk.h silk_sched.h silk_sched_vanilla.h silk_tls.h
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

ping_pong.o: ping_pong.c $(LIB_HDR)
	gcc ping_pong.c $(CFLAGS) $(CFLAGS_TESTS)

ping_pong: ping_pong.o $(LIB_SILK)
	gcc $(LFLAGS) ping_pong.o -o ping_pong $(LIBS) -L /home/bename/devel/silk-uthreads

ut_kill.o: ut_kill.c $(LIB_HDR)
	gcc ut_kill.c $(CFLAGS) $(CFLAGS_TESTS)

ut_kill: ut_kill.o $(LIB_SILK)
	gcc $(LFLAGS) ut_kill.o -o ut_kill $(LIBS) -L /home/bename/devel/silk-uthreads

echo_server.o: echo_server.c echo_sample.h
	gcc echo_server.c $(CFLAGS) $(CFLAGS_TESTS)

echo_server: echo_server.o $(LIB_SILK)
	gcc $(LFLAGS) echo_server.o -o echo_server $(LIBS) -L /home/bename/devel/silk-uthreads

echo_client.o: echo_client.c echo_sample.h
	gcc echo_client.c $(CFLAGS) $(CFLAGS_TESTS)

echo_client: echo_client.o $(LIB_SILK)
	gcc $(LFLAGS) echo_client.o -o echo_client $(LIBS) -L /home/bename/devel/silk-uthreads

tests: run_n ping_pong ut_kill echo_server echo_client
	echo "building all tests"

clean:
	rm -f *.o core $(LIB_SILK) run_n ping_pong ut_kill echo_server echo_client

superclean: clean
	rm -f TAGS cscope.out *~

TAGS:
	etags --output=TAGS *.c *.h

.PHONY: all clean tests
all: tests TAGS 
