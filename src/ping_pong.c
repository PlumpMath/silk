/*
 * Copyight (C) Eitan Ben-Amos, 2012
 *
 * A small test program to dispatch N silks & watch their context switching
 * while the silk instances send msgs from one to another.
 * This means for initial development of the library & basic sanity further on.
 * it also shows how the number of free silks decreases due to their 
 * allocation & increases when they terminate.
 *
 * Execution path
 * after we dispatch N silsk, #i will send a msg to i+1 (cyclic).
 * on second round, #i will send msg to i+2.
 *.....
 * on N-1 round, #i sends the msg to (i+n) module N
 * so each silk awaits N-1 msgs & then terminates.
 * each will also verify it receives a msg from any other silk & at the correct order.
 * and for desert, we also time the execution so different optimizations & schedulers can be profiled.
 *
 * Sample msgs sent for CLI "3 3"
 * since dispatch order is 0,1,2 we'll see
 * from    to
 *-------------
 * 0       1 \
 * 1       2  = Order of dispatch
 * 2       0 /
 * from now the order is set by the order we process msgs of the quue
 * 1       0
 * 2       1
 * 0       2
 * 0       0
 * 1       1
 * 2       2
 *
 * Note: be sure to measure performance with:
 * 1) assertions & logging disabled !!!
 * 2) gcc speed optimization (-Ofast)
 */


#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#define __USE_XOPEN_EXTENDED
#include <unistd.h>
#include "silk.h"


/*
 * The application-speicifc msg we send to the silks
 */
#define SILK_MSG__APP_PING_PONG     SILK_MSG_APP_CODE_FIRST

/*
 * the number of silks to dispatch by default
 */
#define DEFAULT_NUM_SILKS          4
/*
 * the number of yeilds (and msgs) that each silk we dispatch expects to process
 * before it terminates
 */
#define DEFAULT_NUM_YIELD_PER_SILK 1

/*
 * a matrix of size N*N where N is the number of silks we dispatch.
 * entry (i,j) indicate whether i had already received a msg from j
 */
bool    *rcv_msg_flags = NULL;

/*
 * CLI options
 */
struct options {
    int    num_silk;
} opt;

struct silk_engine_t   engine;

/*
 * The information that Silk#A attaches to a msg it sends to Silk#B so that B knows it 
 * was received from A & can also verify the expected msg index.
*/
struct ping_pong_msg_info_t {
    // the Silk who sent the msg
    silk_id_t   msg_src;
    // The msg counter
    int         msg_count;
};

/*
 * count the total msgs to be able to tell when the last silk ends.
 */
int   rcv_msg_count = 0;
/*
 * times taken to measure the processing of these msgs
 */
clock_t   first_silk_starts, last_silk_ends;



static void
ping_pong_idle_cb (struct silk_execution_thread_t   *exec_thr)
{
    struct silk_engine_t                   *engine = exec_thr->engine;

    assert(engine->cfg.ctx == NULL);
    printf("ping pong idle\n");
    sleep(1);
}

static inline silk_id_t
get_prev_id (silk_id_t  id)
{
    if (id == 0) {
        return opt.num_silk-1;
    } else {
        return id-1;
    }
}

static inline silk_id_t
get_next_id (silk_id_t  id)
{
    if (id == opt.num_silk-1) {
        return 0;
    } else {
        return id+1;
    }
}

static void
ping_pong_entry_func (void *_arg)
{
    struct silk_t                 *s = silk__my_ctrl();
    struct silk_msg_t              msg;
    struct ping_pong_msg_info_t   *rx_msg_info;
    struct ping_pong_msg_info_t    tx_msg_info = {
        .msg_src = s->silk_id,
    };
    struct silk_msg_t              tx_msg = {
        .msg = SILK_MSG__APP_PING_PONG,
        .ctx = &tx_msg_info,
    };
    silk_id_t           exp_msg_originator, msg_originator, msg_target;
    enum silk_status_e  silk_stat;
    int arg = (int)_arg;
    int msg_cntr;
    

    SILK_DEBUG("%s: starts... (arg=%d)", __func__, arg);
    if (s->silk_id == SILK_INITIAL_ID) {
        SILK_DEBUG("first silk starts running");
        first_silk_starts = clock();
    }
    exp_msg_originator = get_prev_id(s->silk_id);
    msg_target = get_next_id(s->silk_id);
    for (msg_cntr=0; msg_cntr < opt.num_silk; msg_cntr++) {
        if (msg_cntr < opt.num_silk) {
            SILK_DEBUG("Silk#%d sending msg to Silk#%d",
                       s->silk_id, msg_target);
            tx_msg_info.msg_count = msg_cntr;
            tx_msg.silk_id = msg_target;
            silk_stat = silk_send_msg(&engine, &tx_msg);
            assert(silk_stat == SILK_STAT_OK);
        }
        SILK_DEBUG("Silk#%d expecting msg from Silk#%d",
                   s->silk_id, exp_msg_originator);
        silk_yield(&msg);
        rx_msg_info = (struct ping_pong_msg_info_t*)msg.ctx;
        msg_originator = rx_msg_info->msg_src;
        SILK_DEBUG("Silk#%d got msg # %d from Silk#%d", s->silk_id, msg_cntr, msg_originator);
        assert(msg_originator == exp_msg_originator);
        assert(msg.msg == SILK_MSG__APP_PING_PONG);
        assert(rcv_msg_flags[s->silk_id * opt.num_silk + msg_originator] == false);
        rcv_msg_flags[s->silk_id * opt.num_silk + msg_originator] = true;
        // prepare for next round
        exp_msg_originator = get_prev_id(exp_msg_originator);
        msg_target = get_next_id(msg_target);
        // book-keeping
        rcv_msg_count++;
    }
    if (rcv_msg_count == opt.num_silk * opt.num_silk) {
        SILK_DEBUG("last Silk ends");
        last_silk_ends = clock();
        printf("The running time of %d silks with %d*%d msgs is %ld ticks\n",
               opt.num_silk, opt.num_silk, opt.num_silk, last_silk_ends - first_silk_starts);
    }
    SILK_DEBUG("%s: ends!!!", __func__);
}


 __attribute__((noreturn)) static void usage ()
{
    printf("Usage: ping_pong <num silks>\n");
    exit(EINVAL);
}



int main (int   argc, char **argv)
{
    struct silk_engine_param_t    silk_cfg = {
        .flags = 0,
        // it's possible to select an address or let the library choose
        //.stack_addr = (void*)NULL,
        .stack_addr = (void*)0xb0000000,
        .num_stack_pages = 16,
        .num_stack_seperator_pages = 4,
        .num_silk = DEFAULT_NUM_SILKS,
        .idle_cb = ping_pong_idle_cb,
        .ctx = NULL,
    };
    struct silk_t          *s;
    enum silk_status_e     silk_stat;
    int    i;


    // set CLI defaults
    opt.num_silk = DEFAULT_NUM_SILKS;

    // read CLI options 
    if (argc > 1) {
        if (argc >= 2) {
            opt.num_silk = atoi(argv[1]);
            if (opt.num_silk < 1) {
                usage();
            }
        }
    }

    // allocate flags matrix
    rcv_msg_flags = calloc(opt.num_silk * opt.num_silk, sizeof(*rcv_msg_flags));
    SILK_DEBUG("Initializing Silk engine...");
    silk_cfg.num_silk = opt.num_silk;
    silk_stat = silk_init(&engine, &silk_cfg);
    SILK_DEBUG("Silk initialization returns:%d", silk_stat);
    for (i=0; i < opt.num_silk; i++) {
        silk_stat = silk_alloc(&engine, ping_pong_entry_func, (void*)i, &s);
        assert(silk_stat == SILK_STAT_OK);
        SILK_DEBUG("allocated silk No %d", s->silk_id);
        silk_stat = silk_dispatch(&engine, s);
        assert(silk_stat == SILK_STAT_OK);
        SILK_DEBUG("dispatched silk No %d", s->silk_id);
    }
    // wait for all msgs to be consumed
    while (_silk_sched_is_empty(&engine.msg_sched) == false) { 
        sleep(2);
    }
    /*
     * sleep a bit more to make sure the last Silk marked itself as free (as it has
     * some processing to do when it pops the last msg)
     */
    sleep(1);
    // we expect all silks to be free now
    assert(engine.num_free_silk == opt.num_silk);
    // request engine to terminate
    silk_stat = silk_terminate(&engine);
    SILK_DEBUG("Silk termination returns:%d", silk_stat);

    silk_stat = silk_join(&engine);
    SILK_DEBUG("Silk join returns:%d", silk_stat);
}
