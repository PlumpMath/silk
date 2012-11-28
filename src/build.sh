gcc -std=c99 silk_engine.c -Wall -c -g
gcc -g -Wall silk_engine.o -o silk -l pthread 

