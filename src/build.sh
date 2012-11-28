gcc -std=c99 silk_context.c -Wall -c -g
gcc -std=c99 silk_engine.c -Wall -c -g
gcc -g -Wall silk_engine.o silk_context.o -o silk -l pthread 


# Build the TAGS file
#etags --output=TAGS *.c *.h
