/*
 * Copyight (C) Eitan Ben-Amos, 2012
 *
 * A small test program to dispatch N silks & watch their context switching.
 * This means for initial development of the library & basic sanity further on.
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
 * a ping pong example to measure performance of silk scheduling.
 */
static void
ping_pong_idle_cb (struct silk_execution_thread_t   *exec_thr)
{
    struct silk_engine_t                   *engine = exec_thr->engine;

    assert(engine->cfg.ctx == NULL);
    printf("ping pong idle\n");
    sleep(1); // sleep for 1 sec

    // TODO: replace with real code. for now just exit after some time
}

static void
ping_pong_entry_func (void *_arg)
{
    int arg = (int)_arg;
    int stk_var;
    
    printf("%s: starts... (arg=%d)\n", __func__, arg);
    printf("%s: stack variable address: &stk_var=0x%p\n", __func__, &stk_var);
    usleep(1100 * 1000);
    printf("%s: ends!!!\n", __func__);
}


/*
 * the number of silks to dispatch by default
 */
#define DEFAULT_NUM_SILKS          4
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
    struct silk_engine_t   engine;
    struct silk_t          *s;
    enum silk_status_e     silk_stat;
    int    num_silk = 3;
    int    i;

 
    if (argc > 1) {
        num_silk = atoi(argv[1]);
        if (num_silk < 1) {
            printf("run_n <optional num silks to dispatch>\n");
            exit(EINVAL);
        }
    }


    printf("Initializing Silk engine...\n");
    silk_cfg.num_silk = num_silk;
    silk_stat = silk_init(&engine, &silk_cfg);
    printf("Silk initialization returns:%d\n", silk_stat);
    for (i=0; i < num_silk; i++) {
        silk_stat = silk_alloc(&engine, ping_pong_entry_func, (void*)i, &s);
        assert(silk_stat == SILK_STAT_OK);
        printf("allocacated silk No %d\n", s->silk_id);
        silk_stat = silk_dispatch(&engine, s);
        assert(silk_stat == SILK_STAT_OK);
        printf("dispatched silk No %d\n", s->silk_id);
    }
    for (i=0; i < 2*10; i++) {
        usleep(500*1000);
        SILK_DEBUG("There are %d free silks now", engine.num_free_silk);
    }
    silk_stat = silk_terminate(&engine);
    printf("Silk termination returns:%d\n", silk_stat);
    silk_stat = silk_join(&engine);
    printf("Silk join returns:%d\n", silk_stat);
}
