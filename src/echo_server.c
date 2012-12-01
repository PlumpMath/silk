/*
 * Copyight (C) Eitan Ben-Amos, 2012
 *
 * this is a sample of how to implement a network server which aceppts TCP connection
 * & serves each connection by assigning its own Silk instance. that instance will
 * echo anything that the client sends
 * The BSD sockets API is used for the networking.
 * The IDLE routine is polling for incoming msgs/connect/disconnect & fires the proper
 * msgs to the owning Silk instance in order for it to server the socket.
 * the code shows:
 * 1) how to use the IDLE routine to push new work when the old batch is processed.
 * 2) prioritizing existing work over newly arrived work.
 * 3) fairness in handling incoming work bcz whenever the call to select() returns, we 
 *    dispatch msgs to Silks & finish their processing before we call again to select(). 
 *    this ensures that with each batch of work, we process it before the next batch.
 * 4) Notice we dont queue any data apart from partially received msgs. this is  very 
 *    important for scalability of such a server. if, for example, a second thread was
 *    used to read from the socket & push msgs with data to the Silk engine then a DOS
 *    attack could cause the msg queue to explode. this cannot happen with the design 
 *    seen here.
 * 5) the use of select(,,,timeout) allows us to use the IDLE CPU to get new work while
 *    also preventing busy-wait loop when there's no work coming.
 * 6) Silk-Local-Storage usage
 * Note the deficiency of the code sending the reply. this code is BLOCKING & hence blocks
 * the whole engine. a real-world server would probably do it NON-BLOCKING & maintain 2 fd_set
 * (wr_pending & read_pending) & move a socket from one set to another based on the operation
 * pending on it.
 * an even more scalable solution would use epoll() instead of select()
 * The beuty of coroutines in hiding the complexity of a state machine lies in the simplicity
 * of the silk entry function, echo_server_entry_func (). looking at this function, one can see
 * visually the path of msg processing, starting with readin a fixed length msg header (common 
 * to most high-level protocols), the computing the remaining variablle-length of the msg & 
 * reading it as well & then following with the processing (here, sending the echo). it all 
 * lies as one serial function code even though the bytes of all the msgs are received one by
 * one with a context switch for every byte.
 * running the sample with 2 terminals, each running the client with a sleep/noise time of
 * 1 [sec], allows you to watch the server logging & whatch the 2 sockets being serviced by
 * 2 different Silks, doing the context switch for every byte.
 * TCP msgs have a predefined length limit & start with the length, followed by the string.
 * writing to a sockets is synchronous (i.e.: blocks in case client hangs) while 
 * receiving is async
 */

#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "silk.h"
#include "echo_sample.h"

/*
 * This is the number of Silk instance we'll have.
 * Note: This limits the number of connections we support !!!
 */
#define ECHO_SERVER_MAX_CONN             20

/*
 * max backlog on accepting socket
 */
#define ECHO_SERVER_ACCEPT_MAX_BACKLOG    5

/*
 * The application-speicifc msg we send to the silk in order to trigger him for
 * serving its connection
 */
#define SILK_MSG__READ_SOCKET     SILK_MSG_APP_CODE_FIRST

/*
 * CLI options
 */
struct options {
    // The TCP socket number we'll use to listen for incoming connections
    int    listen_port_num;
} opt;


/*
 * connection-private information. This shows that although a silk has no
 * Silk-Local-Storage (akin to Thread-Local-Storage), it can be easily provided
 * with a separate array of these object. Silk#N will use indec N of that array.
 */
struct conn_state_t {
    // the socket of the connection.
    int    sock;
    // The address of the client on the other side
    struct sockaddr_in    client_addr;
    // the currrent network msg (which might be recived in parts)
    struct echo_msg_t  net_msg;
};
static struct conn_state_t silk_local_stg[ECHO_SERVER_MAX_CONN];

/*
 * The Echo server state
 */
struct echo_server_state_t {
    // The socket which is used to listen & accept new connections
    int    listen_sock;
    // fd set we monitor
    fd_set    fdset_master;
    // fd set used for call
    fd_set    fdset_read;
    // the max fd number we have in the fd_set
    int       max_fd;
};
static struct echo_server_state_t echo_server;

/*
 * mapping the fd number to the silk which serves it
 */
static int map_fd_2_silk_id[__FD_SETSIZE];

/*
 * The Silk engine object
 */
struct silk_engine_t   engine;


#define inline 
#define static 
static inline void
add_sock_to_fdset (int   sock)
{
    FD_SET(sock, &echo_server.fdset_master);
    if (sock > echo_server.max_fd) {
        printf("max_fd changes from %d to %d\n", echo_server.max_fd, sock);
        echo_server.max_fd = sock;
    }
}

static inline void
rm_sock_from_fdset (int   sock)
{
    printf("clearind socket %d from master fd_set\n", sock);
    FD_CLR(sock, &echo_server.fdset_master);
    if (sock == echo_server.max_fd) {
        for (int i = echo_server.max_fd-1; i >= echo_server.listen_sock; i--) {
            if (FD_ISSET(i, &echo_server.fdset_master)) {
                printf("max_fd changes from %d to %d\n", echo_server.max_fd, i);
                echo_server.max_fd = i;
                break;
            }
        }
        // we must find a lower fd as the new max
        assert(echo_server.max_fd < sock);
    }
}


/*
 * a helper function for a Silk to receive a buffer from a socket, possibly in chunks, 
 * yielding the thread for IDLE processing
 */
static int
echo_server_rcv_buf(struct conn_state_t   *conn,
                    void                  *_buf,
                    int                    size)
{
    struct silk_msg_t    msg;
    char  *buf = (char *)_buf;
    int   rcv_len;
    int   rc;


    for (rcv_len = 0; rcv_len < size;) {
        printf("reading from socket...\n");
        rc = recv(conn->sock, buf + rcv_len, size - rcv_len, MSG_DONTWAIT);
        switch (rc) {
        case -1:
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                // we'll handle it after the next call to select()
            } else {
                printf("Failed to read msg header from socket. err=%d\n", errno);
                goto socket_close;
            }
            break;
        case 0: // ordered socket shutdown
            printf("client disconnected\n");
            goto socket_close;
        default: // rc is number of bytes read
            printf("rcv %d bytes\n", rc);
            rcv_len += rc;
            assert(rc <= size);
            if (rcv_len == size) {
                printf("completed reading buffer of size %d\n", size);
                return 0;
            }
            break;
        }
        silk_yield(&msg);
    }

 socket_close:
    return -1;
}

/*
 * The Silk entry function, serving a newly accepted connection & maintaining the state of 
 * the connection.
 */
static void
echo_server_entry_func (void *_arg)
{
    struct silk_t         *s = silk__my_ctrl();
    struct silk_msg_t      msg;
    struct echo_msg_t      net_msg;
    uint16_t   text_len;
    struct conn_state_t   *conn = &silk_local_stg[s->silk_id];


    printf("Silk#%d starting to serve client %s\n", s->silk_id, 
           inet_ntoa(conn->client_addr.sin_addr));
    memset(&conn->net_msg, 0, sizeof(conn->net_msg));

    do {
        printf("Silk#%d Waiting for a msg from the client ...\n", s->silk_id);
        silk_yield(&msg);
        printf("Silk#%d received msg. starting to read msg from client ...\n", s->silk_id);
        if (echo_server_rcv_buf(conn, &net_msg.hdr, sizeof(net_msg.hdr)) < 0) {
            goto socket_close;
        }
        text_len = ntohs(net_msg.hdr.len);
        printf("msg hdr completed. len=%d\n", (unsigned)text_len);

        printf("Reading the msg text...\n");
        if (echo_server_rcv_buf(conn, net_msg.msg, text_len) < 0) {
            goto socket_close;
        }
        printf("msg rcv completed. len=%d, text=%s\n", text_len, net_msg.msg);
        printf("Send echo to client\n");
        if (send(conn->sock, &net_msg, sizeof(net_msg.hdr) + text_len, 0) < 0) {
            printf("failed to write to socket. err=%d\n", errno);
            goto socket_close;
        }
    } while (1);
    
 socket_close:
    printf("client disconnected on fd %d. Silk#%d terminates\n",
           conn->sock, s->silk_id);
    close(conn->sock);
    map_fd_2_silk_id[conn->sock] = 0xffffffff;//poison
    rm_sock_from_fdset(conn->sock);
    printf("Silk#%d terminating\n", s->silk_id);
}


static void
echo_server_idle_cb (struct silk_execution_thread_t   *exec_thr)
{
    struct silk_t        *s;
    enum silk_status_e    silk_stat;
    struct sockaddr_in    client_addr;
    int                   new_conn;
    int                   i, rc;
    socklen_t             client_len;
    struct timeval        max_time;


    /*
     * block waiting for work. if timeout expires, return to let the Silk engine work.
     * we'll be called again once the Silk engine is IDLE (possibly immediately).
     */
    max_time.tv_sec = 1;
    max_time.tv_usec = 0;
    echo_server.fdset_read = echo_server.fdset_master;
    rc = select(echo_server.max_fd + 1, &echo_server.fdset_read, NULL, NULL, &max_time);
    switch (rc) {
    case 0 : // timeout reached. no ocket is ready

        break;

    case -1:
        printf("ERROR: select() failed with %d\n", errno);
        break;

    default:
        printf("Need to serve %d sockets\n", rc);

        for (i=0; i<=echo_server.max_fd; i++) {
            if (FD_ISSET(i, &echo_server.fdset_read)) {
                printf("socket %d needs service\n", i);
                if (i == echo_server.listen_sock) {
                    // socket being service is the one listening for incoming connections
                    client_len = sizeof(client_addr);
                    new_conn = accept(echo_server.listen_sock, (struct sockaddr *)&client_addr,
                                      &client_len);
                    if (new_conn < 0) {
                        printf("ERROR: failed to accept a connection. errno=%d\n", errno);
                    } else {
                        printf("accepted a new connection from %s:%d\n",
                               inet_ntoa(client_addr.sin_addr),
                               client_addr.sin_port);
                        // allocate a new silk to serve the new connection
                        silk_stat = silk_alloc(&engine, echo_server_entry_func, (void*)new_conn, &s);
                        assert(silk_stat == SILK_STAT_OK);
                        SILK_DEBUG("allocated silk No %d", s->silk_id);
                        // copy connection info/state into silk-local-storage
                        struct conn_state_t   *conn = &silk_local_stg[s->silk_id];
                        conn->sock = new_conn;
                        conn->client_addr = client_addr;
                        add_sock_to_fdset(new_conn);
                        map_fd_2_silk_id[new_conn] = s->silk_id;
                        // dispatch the silk in order to server the connection
                        silk_stat = silk_dispatch(&engine, s);
                        assert(silk_stat == SILK_STAT_OK);
                        SILK_DEBUG("dispatched silk No %d", s->silk_id);
                    } // accept is successfull
                } else {
                    // socket is a client connection
                    silk_stat = silk_send_msg_code(&engine, SILK_MSG__READ_SOCKET, 
                                                   map_fd_2_silk_id[i]);
                    assert(silk_stat == SILK_STAT_OK);
                }
            }
        }
    }
}



static void usage ()
{
    fprintf(stderr, "Usage: echo_server -l <listening TCP port>\n");
    exit(-EINVAL);
}

int main (int   argc, char **argv)
{
    static struct silk_engine_param_t    silk_cfg = {
        .flags = 0,
        .stack_addr = (void*)0xb0000000,
        .num_stack_pages = 16,
        .num_stack_seperator_pages = 4,
        .num_silk = ECHO_SERVER_MAX_CONN,
        .idle_cb = echo_server_idle_cb,
        .ctx = NULL,
    };
    enum silk_status_e     silk_stat;
    struct sockaddr_in     sock_addr;
    int     c;

    // set CLI defaults
    opt.listen_port_num = ECHO_DERVER_DEFAULT_PORT;

    // read CLI options 
    while ((c = getopt(argc, argv, "l:h?")) != -1) {
        switch (c) {
        case 'l':
            opt.listen_port_num = atoi(optarg);
            break;
        case 'h':
            usage();
            break;
        default:
            usage();
        }
    }

    // set globals
    FD_ZERO(&echo_server.fdset_master);
    echo_server.max_fd = 0;
    memset(map_fd_2_silk_id, 0xff, sizeof(map_fd_2_silk_id)); // poison mapping

    // initialize our Silk engine
    silk_stat = silk_init(&engine, &silk_cfg);
    SILK_DEBUG("Silk initialization returns:%d", silk_stat);

    // create the socket we use for listening, bind & listen
    echo_server.listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (echo_server.listen_sock < 0) {
        printf("ERROR: failed to create a TCP socket\n");
        exit(errno);
    }
    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(opt.listen_port_num);
    sock_addr.sin_addr.s_addr = INADDR_ANY; // choose any IP on muliti-homed server
    if (bind(echo_server.listen_sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
        printf("ERROR: failed to bind. errno=%d\n", errno);
        exit(errno);
    }
    if (listen(echo_server.listen_sock, ECHO_SERVER_ACCEPT_MAX_BACKLOG) < 0) {
        printf("ERROR: failed to listen. errno=%d", errno);
        exit(errno);
    }
    add_sock_to_fdset(echo_server.listen_sock);
    printf("echo_server is listening for incoming connections...\n");

    // sleep forever, serving connections.
    do {
        sleep(1);
    } while (1);
    
    return 0;
}
