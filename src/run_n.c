/*
 * Copyight (C) Eitan Ben-Amos, 2012
 *
 * A small test program to dispatch N silks & watch their context switching.
 * This was meant for initial development of the library & basic sanity further on.
 * it also shows how the number of free silks decreases due to their 
 * allocation & increases when they terminate.
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
#define SILK_MSG__APP_RUN_N     SILK_MSG_APP_CODE_FIRST

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
 * indicate how many msgs should every silk instance expect to receive
 */
int    num_yields = DEFAULT_NUM_YIELD_PER_SILK;


/*
 * a simplest example to create a few silks & show the library's scheduling.
 */
static void
run_n__idle_cb (struct silk_execution_thread_t   *exec_thr)
{
    struct silk_engine_t                   *engine = exec_thr->engine;

    assert(engine->cfg.ctx == NULL);
    printf("run_n idle\n");
    sleep(1); // sleep for 1 sec
}

static void
run_n__entry_func (void *_arg)
{
    struct silk_t                  *s = silk__my_ctrl();
    struct silk_msg_t   msg;
    int arg = (int)_arg;
    int stk_var;
    int msg_cntr;
    
    printf("%s: starts... (arg=%d)\n", __func__, arg);
    printf("%s: stack variable address: &stk_var=0x%p\n", __func__, &stk_var);

    for (msg_cntr=0; msg_cntr < num_yields; msg_cntr++) {
        SILK_DEBUG("Silk#%d waiting for msg # %d", s->silk_id, msg_cntr);
        silk_yield(&msg);
        SILK_DEBUG("Silk#%d got msg # %d", s->silk_id, msg_cntr);
        assert(msg.msg == SILK_MSG__APP_RUN_N);
        usleep(1100 * 1000);
    }
    printf("%s: ends!!!\n", __func__);
}


 __attribute__((noreturn)) static void usage ()
{
    printf("Usage: run_n <num silks> <num yields per silk>\n");
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
        .idle_cb = run_n__idle_cb,
        .ctx = NULL,
    };
    struct silk_engine_t   engine;
    struct silk_t          *s;
    enum silk_status_e     silk_stat;
    int    num_silk = 3;
    int    batch;
    int    i;

 
    if (argc > 1) {
        if (argc >= 2) {
            num_silk = atoi(argv[1]);
            if (num_silk < 1) {
                usage();
            }
        }
        if (argc >= 2) {
            num_yields = atoi(argv[2]);
            if (num_yields <= 1) {
                usage();
            }
        }
    }


    printf("Initializing Silk engine...\n");
    silk_cfg.num_silk = num_silk;
    silk_stat = silk_init(&engine, &silk_cfg);
    printf("Silk initialization returns:%d\n", silk_stat);
    for (i=0; i < num_silk; i++) {
        silk_stat = silk_alloc(&engine, run_n__entry_func, (void*)i, &s);
        assert(silk_stat == SILK_STAT_OK);
        printf("allocacated silk No %d\n", s->silk_id);
        silk_stat = silk_dispatch(&engine, s);
        assert(silk_stat == SILK_STAT_OK);
        printf("dispatched silk No %d\n", s->silk_id);
    }
    for (batch=0; batch < num_yields; batch ++) {
        SILK_DEBUG("--------------Dispatching batch # %d of msgs---------------", batch);
        for (i=0; i < num_silk; i++) {
            silk_stat = silk_send_msg_code(&engine, SILK_MSG__APP_RUN_N, i);
            assert(silk_stat == SILK_STAT_OK);
        }
        SILK_DEBUG("Waiting for msg to be processed.");
        for (i=0; i < 2*10; i++) {
            usleep(500*1000);
            SILK_DEBUG("There are %d free silks now", engine.num_free_silk);
        }
    }
    sleep(2);
    // we expect all silks to complete execution by now
    assert(_silk_sched_is_empty(&engine.msg_sched) == true);
    // we expect all silks to be free now
    assert(engine.num_free_silk == num_silk);

    silk_stat = silk_terminate(&engine);
    printf("Silk termination returns:%d\n", silk_stat);
    silk_stat = silk_join(&engine);
    printf("Silk join returns:%d\n", silk_stat);
}
