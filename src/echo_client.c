/*
 * Copyight (C) Eitan Ben-Amos, 2012
 *
 * this is a the client program to work against the echo_server example. it will
 * open a connection & post msgs, reading the reply for each msg
 */

#define _BSD_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "echo_sample.h"



#define NUM_MSGS_TO_SERVER     3

/*
 *the msgs that this client will send to the server
 */
static const char *msgs_for_server[NUM_MSGS_TO_SERVER] = {
    "First Message",
    "The Second",
    "I'm Third & last one"
};



static void usage()
{
    fprintf(stderr, "Usage: echo_client [-s <server>] [-p <server port>] [-n <noise [msec]>]\n");
    fprintf(stderr, "Default server: localhost\n");
    fprintf(stderr, "Default port: %d", ECHO_DERVER_DEFAULT_PORT);
    fprintf(stderr, "Default noise: 0 (i.e.: no noise! each msg is sent in one take\n");
    exit(-EINVAL);
}

/*
 * send the buffer over the socket. if noise_time isnt 0 then randomly break in-between to 
 * cause network fragmentation (or just send them byte by byte)
 */
static void
send_buffer(int         sock,
            const void *buf,
            int         size,
            int         noise_time)
{
    const char *p;


    for (p = buf; p < (const char *)buf + size; ) {
        if (noise_time != 0) {
            //chunk_size = rand() % (len + 1);
        }
        
        if (send(sock, p, 1, MSG_DONTWAIT) < 0) {
            printf("failed to write to socket. err=%d\n", errno);
            exit(errno);
        }
        p++;
        if (noise_time) {
            usleep(noise_time);
        }
    }
}

int main (int   argc, char **argv)
{
    int   port_num, sock;
    static const char      default_server_name[] = "localhost";
    const char            *server_name;
    int                   noise_time;
    struct hostent        *server_addr;
    struct sockaddr_in     sock_addr;
    struct echo_msg_t      msg;
    int                    text_len;
    time_t                 random_seed;
    int     c;


    // set CLI defaults
    port_num = ECHO_DERVER_DEFAULT_PORT;
    server_name = default_server_name;
    noise_time = 0;

    // read CLI options 
    while ((c = getopt(argc, argv, "s:p:n:h?")) != -1) {
        switch (c) {
        case 's':
            server_name = optarg;
        case 'p':
            port_num = atoi(optarg);
            break;
        case 'n': // put noise into transmission
            noise_time = 1000*atoi(optarg);
            break;
        case 'h':
            usage();
            break;
        default:
            usage();
        }
    }
    server_addr = gethostbyname(server_name);
    if (server_addr == NULL) {
        printf("Failed to find server: %s\n", server_name);
        usage();
    }

    // initialize random library for noised transmission
    random_seed = time(NULL);
    printf("Initializing RNG seed with %ld\n", random_seed);
    srand(random_seed);
    // create the socket we use for communicating with the server
    printf("connecting to %s\n", server_name);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("ERROR: failed to create a TCP socket\n");
        exit(errno);
    }
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port_num);
    memcpy(&sock_addr.sin_addr.s_addr, server_addr->h_addr_list[0], server_addr->h_length);
    if (connect(sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
        printf("failed to connect\n");
        exit(EIO);
    }
    printf("Connected ....\n");

    for (int i=0; i < NUM_MSGS_TO_SERVER; i++) {
        text_len = strlen(msgs_for_server[i]);
        if (text_len >= sizeof(msg.msg)) {
            printf("msg %d too long\n", i);
            exit(EIO);
        }
        msg.hdr.len = 1 + text_len;
        strcpy(msg.msg, msgs_for_server[i]);

        printf("Sending msg: len=%d, text=%s\n", msg.hdr.len, msg.msg);
        send_buffer(sock, &msg, sizeof(msg.hdr) + msg.hdr.len, noise_time);

        if (recv(sock, &msg, sizeof(msg.hdr) + msg.hdr.len, 0) < 0) {
            printf("failed to read from socket. err=%d\n", errno);
            exit(errno);
        }
        printf("reading msg: len=%d, text=%s\n", msg.hdr.len, msg.msg);
    }

    return 0;
}
