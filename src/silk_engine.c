/*
 * Copyight (C) Eitan Ben-Amos, 2012
 */

#include <memory.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#define __USE_XOPEN_EXTENDED
#include <unistd.h> 
#define __USE_MISC
#include <sys/mman.h>
#include "silk.h"
 




/*
 * Queries the engine object whether it has completed its initialization & is ready to work
 */
static inline bool
silk_eng_is_ready (struct silk_engine_t      *engine)
{
    bool   ret;

    pthread_mutex_lock(&engine->mtx);
    if (engine->num_free_silk == engine->cfg.num_silk) {
        ret = true;
    } else {
        ret = false;
    }
    pthread_mutex_unlock(&engine->mtx);

    return ret;
}


static void
silk_eng_add_free_silk(struct silk_engine_t       *engine,
                       struct silk_t              *s)
{
    silk__set_state(s, SILK_STATE__FREE);
    pthread_mutex_lock(&engine->mtx);
    engine->num_free_silk++;
    /*
     * push the terminated silk instance to head of queue. better for CPU cache behavior
     * p.s. : it will also reveal misuse of silk stack after its termination much faster.
     */
    SLIST_INSERT_HEAD(&engine->free_silks, s, next_free);
    pthread_mutex_unlock(&engine->mtx);
}


/*
 * This is the internal entry function of all silks.
 * a silk uthread starts its life here & then allocated, runs & 
 * Note:
 * The function prototype MUST be maintained the same as the function that switches context bcz we start this function by "switching context" into it. DO WE REALLY NEED IT SO ?????
 */
static void silk__main (void) /*__attribute__((no_return))*/
{
    struct silk_execution_thread_t         *exec_thr = silk__my_thread_obj();
    struct silk_engine_t                   *engine = exec_thr->engine;
    struct silk_msg_t       msg;
    struct silk_t           *s = silk__my_ctrl();


    SILK_DEBUG("Silk#%d booting...", s->silk_id);
    assert(SILK_STATE(s) == SILK_STATE__BOOT);

    // wait for the BOOT msg
    silk_yield(&msg);
    assert(msg.msg == SILK_MSG_BOOT);

    SILK_DEBUG("Silk#%d processing BOOT msg", s->silk_id);
    assert(SILK_STATE(s) == SILK_STATE__BOOT);
    silk__set_state(s, SILK_STATE__FREE);
    pthread_mutex_lock(&engine->mtx);
    engine->num_free_silk++;
    pthread_mutex_unlock(&engine->mtx);
    
    do {
        // wait for the START msg
        silk_yield(&msg);
        /* 
         * handle the 3 possible msgs using "if" rather than "switch" bcz the
         * probability of each is drastically lower than the one checked before hand.
         */
        if (likely(msg.msg == SILK_MSG_START)) {
            s = silk_get_ctrl_from_id(engine, msg.silk_id);
            SILK_INFO("state= %d", SILK_STATE(s));
            assert(SILK_STATE(s) == SILK_STATE__ALLOC);
            SILK_INFO("silk %d starting", silk__my_id());
            silk__set_state(s, SILK_STATE__RUN);
            s->entry_func(s->entry_func_arg);
            assert(SILK_STATE(s) == SILK_STATE__RUN);
#if 1
            silk_eng_add_free_silk(engine, s);
#else
            silk__set_state(s, SILK_STATE__FREE);
            pthread_mutex_lock(&engine->mtx);
            engine->num_free_silk++;
            pthread_mutex_unlock(&engine->mtx);
            /*
             * push the terminated silk instance to head of queue. better for CPU cache behavior
             * p.s. : it will also reveal misuse of silk stack after its termination much faster.
             */
            SLIST_INSERT_HEAD(&engine->free_silks, s, next_free);
#endif
            SILK_INFO("silk %d ended", silk__my_id());
        } else if (likely(msg.msg == SILK_MSG_TERM)) {
            SILK_INFO("Silk#%d processing TERM msg", s->silk_id);
            /*
             * BEWARE: we push the silk on which we currently execute to the free list.
             * This is OK as long as the addition of the silk to the free list is
             * protected by a lock so no other thread can allocate it & fire it while
             * we use its stack.
             */
            assert(0); // we can only get here for popin the TERM msg on a silk in state ALLOC.
        } else if (unlikely(msg.msg == SILK_MSG_TERM_THREAD)) {
            SILK_INFO("kernel thread %lu processing TERM msg", exec_thr->id);
            engine->terminate = true;
        } else {
            /*
             * we've just poped a msg that was destined to a silk that doesnt expect it.
             * we just drop it.
             */
            SILK_INFO("Got unexpected msg - dropping!!! msg={code=%d, id=%d, ctx=%p}",
                      msg.msg, msg.silk_id, msg.ctx);
        }
    } while (likely(engine->terminate == false));
    SILK_INFO("thread %lu switching back to pthread stack", exec_thr->id);
    SILK_SWITCH(exec_thr->exec_state, s->exec_state);
}

/*
 * This is the entry function for a pthread that is used to execute uthreads.
 * we first switch into a uthread & then jump from one uthread to another WITHOUT ever 
 * going back to the pthread-provided stack (we might want that for IDLE callback processing)
 * when the engine is terminated, the thread will switch from the last uthread is executed to
 * the original pthread stack frame (in which this function started to execute) & this
 * function will then return & terminate the posix thread.
 */
static void *silk__thread_entry(void *ctx)
{
    struct silk_execution_thread_t         *exec_thr = (struct silk_execution_thread_t*)ctx;
    struct silk_engine_t                   *engine = exec_thr->engine;
    struct silk_t       *s;
    const silk_id_t      silk_id = SILK_INITIAL_ID; // the silk we'll start to run on.


    SILK_INFO("Thread starting. id=%lu", exec_thr->id);
    // set the Silk thread object so every thread to hs access to its own
    silk__set_tls(SILK_TLS__THREAD_OBJ, exec_thr);
    /*
     * switch into one silk (no matter which) to start processing msgs. from that
     * point onwards, we'll only switch from one silk to another without ever 
     * switching back to the original pthread-provided stack.
     * although any silk instance would do, we take the INITIAL_ID bcz it is 
     * implicitly processing a BOOT msg so the initialization could dropsa BOOT
     *  msg destined to the the INITIAL_ID
     * p.s.
     * one might want to consider calling the IDLE function using the pthread stack.
     * but this implies a little extra overhead. it might be worth it in case that
     * IDLE work requires a much larger stack than the one used by the silks (increasing
     *  the stack for all silks would be a huge waste of memory).
     * BEWARE: we are swapping into a silk which is designated "free" & use its stack
     * for our execution until we switch into a silk for processng its msg.
     */
    s = silk_get_ctrl_from_id(engine, silk_id);
    SILK_SWITCH(s->exec_state, exec_thr->exec_state);
    // we get here only if the engine is terminating !!!
    SILK_INFO("Thread exiting. id=%lu", exec_thr->id);
    return NULL;
}

enum silk_status_e
silk_thread_init (struct silk_execution_thread_t        *exec_thr,
                  struct silk_engine_t                  *engine)
{
    int          rc;
 
		
    exec_thr->engine = engine;
    rc = pthread_create(&exec_thr->id, NULL, silk__thread_entry, exec_thr);
    if (rc != 0) {
        return SILK_STAT_THREAD_CREATE_FAILED;
    }
    return SILK_STAT_OK;
}


/*
 * request the engine to terminate gracefully.
 */
static inline enum silk_status_e
silk_eng_terminate (struct silk_execution_thread_t       *exec_thr)
{
    return silk_send_msg_code(exec_thr->engine, SILK_MSG_TERM_THREAD, 0/* doesnt matter - its for the engine itself*/);
}

/*
 * wait for the internal thread to terminate & only then return. this ensures the
 * caller that once the API returns, it can free whatever resources were bound to
 * the engine.
 */
enum silk_status_e
silk_eng_join (struct silk_execution_thread_t                    *exec_thr)
{
    int rc;
    rc = pthread_join(exec_thr->id, NULL);
    if (rc == 0)
        return SILK_STAT_OK;
    return SILK_STAT_THREAD_ERROR;
}

 

enum silk_status_e
silk_init (struct silk_engine_t               *engine,
           const struct silk_engine_param_t   *param)
{
    enum silk_status_e     ret;
    size_t                 stack_size;
    int                    mem_flags;
    void                   *addr;
    struct silk_t          *s;
    int                    i, rc;
 

    if (param->num_stack_pages < 1)
        return SILK_STAT_INVALID_STACK_SIZE;
    if (param->num_silk < SILK_MIN_NUM_THREADS)
        return SILK_STAT_INVALID_NUM_SILK;

    memset(engine, 0, sizeof(*engine));
    memcpy(&engine->cfg, param, sizeof(engine->cfg));
    engine->terminate = false;
    engine->num_free_silk = 0;
    pthread_mutex_init(&engine->mtx,NULL);
    SLIST_INIT(&engine->free_silks);

    /*
     * allocate memory for stacks
     * memory layout would be
     * |stack for uthread 0 | unmapped (protection) pages | stack for uthread 1 | unmapped (protection) pages | ….
     * all memory is initially allocated as "No Access" & only the alowed 
     * areas will be allowed on top of that.
     */
    stack_size = SILK_PADDED_STACK(param) * param->num_silk;
    mem_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK;
    if (param->stack_addr != NULL) {
        mem_flags |= MAP_FIXED;
    }
    /* 
     * configuration indicate whether to lock the stacks area into memory or 
     * not. This also requires permissions
     */
    if (param->flags & SILK_CFG_FLAG_LOCK_STACK_MEM) {
        mem_flags |= MAP_LOCKED;
    }
    mem_flags |= MAP_POPULATE;// TODO: consider this as config param as it consumes lots of memory. will be meaningfull only if terminated Silks are pushed to the head of the free Silk queue.
    // TODO: what about MAP__NORESERVE to save swap space?
    // TODO: x86 stack grows downward - so we probably need a protection page before the first stack area.
    engine->stack_addr = mmap(param->stack_addr, stack_size, PROT_NONE, 
                              mem_flags, -1 /* ignored*/, 0);
    if (engine->stack_addr == MAP_FAILED) {
        SILK_ERROR("Failed to allocate stack memory area. errno=%d", errno);
        ret = SILK_STAT_STACK_ALLOC_FAILED;
        goto stack_alloc_fail;
    }
    // set each Silk instance stack area to PROT_WRITE
    for (i=0, addr=engine->stack_addr;
         i < param->num_silk;
         i++, addr += SILK_PADDED_STACK(param)) {
        rc = mprotect(addr, param->num_stack_pages * PAGE_SIZE, PROT_WRITE);
        if (rc != 0) {
            SILK_ERROR("Failed to set stack memory protection. errno=%d",
                       errno);
            ret = SILK_STAT_STACK_PROTECTION_SCHEME_FAILED;
            goto stack_prot_fail;
        }
    }

    // allocate per silk instance context information
    engine->silks = calloc(param->num_silk, sizeof(*engine->silks));
    if (engine->silks == NULL) {
        ret = SILK_STAT_ALLOC_FAIL;
        goto silk_state_alloc_fail;
    }

    // initialize the msg queue object
    ret = silk_sched_init(&engine->msg_sched);
    if (ret != SILK_STAT_OK) {
        goto msg_q_init_fail;
    }

    // initialize per silk control & set context to the internal entry function
    for (i=0, addr=engine->stack_addr, s=engine->silks;
         i < param->num_silk;
         i++, addr += SILK_PADDED_STACK(param), s++) {
        // chain the silk instance into the free list. we chain them by their silk_ID :)
        if (i == 0) {
            SLIST_INSERT_HEAD(&engine->free_silks, s, next_free);
        } else {
            SLIST_INSERT_AFTER(s-1, s, next_free);
        }
        // mark the silk control state as in BOOT phase
        silk__set_state(&engine->silks[i], SILK_STATE__BOOT);
        // initialize the unique silk-ID within the silk control object
        engine->silks[i].silk_id = i;
        assert(addr == silk_get_stack_from_id(engine, i));
        // initialize stack context for each silk instance
        silk_create_initial_stack_context(&s->exec_state,
                                          silk__main,
                                          addr,//silk_get_stack_from_id(engine, msg.silk_id),
                                          SILK_USEABLE_STACK(&engine->cfg));
        /*
         * ask each silk to boot into the place they all wait for msgs
         * The first msg causes the scheduler to switch into it. it then runs from the 
         * beggining of silk__main() up to the first silk_yield() where it joins all
         * silks awaiting control msgs. however, the msg itself wasnt processed bcz
         * it was lost when we switched into silk__main() which isnt built to retrieve
         * the msg which cause the context-switch into it.
         * the second msg is received by each silk instance & causes it to return from
         * the call to silk_yield(). this one is required only bcz the silk instance
         * state-machine requires a BOOT msg in order to change its state to FREE.
         * The second msg isnt sent to the INITIAL silk bcz this one executed its part
         * from silk__main() to silk_yield() as part of the pthread initialization. the
         * pthread switches into the INITIAL silk & this allows it to run up to the 
         * call to silk_yield().
         */
        ret = silk_send_msg_code(engine, SILK_MSG_BOOT, s->silk_id);
        assert(ret == SILK_STAT_OK);
        if (i != SILK_INITIAL_ID) {
            ret = silk_send_msg_code(engine, SILK_MSG_BOOT, s->silk_id);
            assert(ret == SILK_STAT_OK);
        }
    }
    assert(_silk_sched_get_q_size(&engine->msg_sched) == 2*param->num_silk-1);
    
    // let the thread execution start
    ret = silk_thread_init(&engine->exec_thr, engine);
    if (ret != SILK_STAT_OK) {
        goto thread_init_fail;
    }

    // wait until all BOOT msgs are processed.
    while (!silk_eng_is_ready(engine)) {
        SILK_DEBUG("waiting for all BOOT msgs to be processed");
        usleep(10);
    }
    SILK_DEBUG("All Silks completed booting !");
    assert(_silk_sched_is_empty(&engine->msg_sched) == true);

    return SILK_STAT_OK;

 thread_init_fail:
    silk_sched_terminate(&engine->msg_sched);
 msg_q_init_fail:
    free(engine->silks);
 silk_state_alloc_fail:
 stack_prot_fail:
    rc = munmap(param->stack_addr, stack_size);
    if (rc != 0) {
        SILK_ERROR("Failed to unmap stack area memory. errno=%d", errno);
        /* were already in error handling path - continue as if no error */
    }
 stack_alloc_fail:
    pthread_mutex_destroy(&engine->mtx);

    return ret;
}


/*
 * This API allows the scheduler to take the calling Silk out-of-execution & switch 
 * to another silk instance. the specifics of such a decision is scheduler-specific.
 * When the function returns, the msg which awoke the silk is returned so it can be 
 * processed
 * The code loops on the IDLE callback & whenever it return it attempts to pop a msg
 * for execution. once a msg is available for execution, we switch into the Silk who
 * should get the msg & deliver the msg to it.
 */
void silk_yield(struct silk_msg_t   *msg)
{
    struct silk_execution_thread_t *exec_thr = silk__my_thread_obj();
    struct silk_engine_t           *engine = exec_thr->engine;
    struct silk_t                  *s = silk__my_ctrl();
    struct silk_t                  *silk_trgt;
    silk_id_t                       msg_silk_id;
    bool                            is_msg_avail = false;
    enum silk_status_e              ret;


    do {
        /*
         * retrieve the next msg (based on priorities & any other application
         * specific rule) to be processed.
         */
        if (silk_sched_get_next(&exec_thr->engine->msg_sched, &exec_thr->last_msg)) {
            {// debugging aid
                struct silk_msg_t                     *m = &exec_thr->last_msg;
                SILK_DEBUG("recv msg={code=%d, id=%d, ctx=%p}", m->msg, m->silk_id, m->ctx);
            }
            msg_silk_id = exec_thr->last_msg.silk_id;
            silk_trgt = &engine->silks[msg_silk_id];
            assert(silk_trgt->silk_id == msg_silk_id);
            /*
             * check if we got a msg instructing us to kill the silk instance. if so, no
             * point to switch into it. just recycle it.
             */
            if (unlikely(exec_thr->last_msg.msg == SILK_MSG_TERM)) {
                SILK_DEBUG("recycling a terminated Silk#%d", silk_trgt->silk_id);
                silk__set_state(silk_trgt, SILK_STATE__BOOT);
                SLIST_INSERT_HEAD(&engine->free_silks, silk_trgt, next_free);
                printf("!!! FIX ME !!! silk initialization code a bit duplicated above (SLIST)\n");
                // initialize stack context bcz the silk should start from a clean stack.
                silk_create_initial_stack_context(&silk_trgt->exec_state,
                                                  silk__main,
                                                  silk_get_stack_from_id(engine, msg_silk_id),
                                                  SILK_USEABLE_STACK(&engine->cfg));
                // let the re-initialized silk to run till it awaits the START msg
                ret = silk_send_msg_code(engine, SILK_MSG_BOOT, silk_trgt->silk_id);
                assert(ret == SILK_STAT_OK);
                /*
                 * we've just sent BOOT msgs to the silk & now we need it to start running
                 * from the entry function. 
                 * Case 1: were recycling the silk who's stack is now in use
                 * switch into the freshly initialized silk to make it run from silk__main()
                 * until the first silk_yield(). it will then process the MSG_BOOT we've
                 * just sent it & reach the point where it is ready to take a MSG_START
                 * Note that once were on that silk, we'll use its fresh stack to pump 
                 * the next msg
                 * BEWARE: in this case we are already on that terminated silk & hence 
                 * we work on the same stack but on lower addresses compared with whah we
                 * now initialize.
                 * Case 2: were recycling a different silk instance the the one who's stack
                 * is in use right now.
                 */
                if (s->silk_id == msg_silk_id) {
                    // we recycle the silk on which we are currently running.
                    SILK_SWITCH(silk_trgt->exec_state, s->exec_state);
                    assert(0); // we should NOT return from the switch.
                } else {
                    /*
                     * we recycle a silk when running on the stack of a different silk.
                     * just continue pumping msgs & when the BOOT msg we just sent is poped
                     * we will naturaly switch into that (recycled) silk & boot it.
                     */
                    ret = silk_send_msg_code(engine, SILK_MSG_BOOT, silk_trgt->silk_id);
                    assert(ret == SILK_STAT_OK);
                    continue;
                }
            }

            /*
             * check whether the silk is pending termination. if so then this is
             * the time to execute !
             */
            if (unlikely(SILK_STATE(silk_trgt) == SILK_STATE__TERM)) {
                SILK_DEBUG("dropping a msg bcz silk is killed, pending recycle");
                continue;
            }

            /*
             * swap context into the silk which received the msg.
             * BEWARE: 
             * 1) 's' was set in previous loop iteration & hence it has the older silk 
             * object, which executed the last msg
             * 2) we must  optimize the case where we can skip switching from one instance
             *    to itself. otherwise, we'll send the "current" stack-pointer as 
             *    target & save a stack-pointer which is pushed into while saving the
             *    state. it will cause us to return on the stack as if "before" we saved
             *    the state. very bad !!!
             */
            if (likely(s->silk_id != msg_silk_id)) {
                SILK_DEBUG("switching from Silk#%d to Silk#%d",
                           s->silk_id, silk_trgt->silk_id);
                SILK_SWITCH(silk_trgt->exec_state, s->exec_state);
                SILK_DEBUG("switched into Silk#%d", s->silk_id);
            }
            memcpy(msg, &exec_thr->last_msg, sizeof(*msg));
            is_msg_avail = true;
        } else { 
            // IDLE processing
            engine->cfg.idle_cb(exec_thr);
        }
    } while (!is_msg_avail);
}

/*
 * drain msgs of a silk instance indefinitely.
 */
__attribute__((noreturn)) void silk_drain_msgs()
{
    struct silk_msg_t    msg;
    //enum silk_status_e   silk_stat;

    do {
        silk_yield(&msg);
    } while (1);
}

/*
 * notifies the engine to terminate itself (i.e.: stop processing & be ready for
 * cleanup).
 * BEWARE: once this API is called, you must wait until the engine has stopped
 * procesing msgs !!!
 */
enum silk_status_e
silk_terminate(struct silk_engine_t   *engine)
{
    silk_eng_terminate(&engine->exec_thr);

    return SILK_STAT_OK;
}
 

/*
 * wait for the thread to terminate & free all resources allocated to an engine.
 */
enum silk_status_e
silk_join(struct silk_engine_t   *engine)
{
    struct silk_engine_param_t   *cfg = &engine->cfg;
    size_t              stack_size;
    enum silk_status_e  ret;
    int     rc;


    ret = silk_eng_join(&engine->exec_thr);
    if (ret != SILK_STAT_OK)
        return ret;
    free(engine->silks);
    stack_size = SILK_PADDED_STACK(cfg) * cfg->num_silk;
    rc = munmap(engine->stack_addr, stack_size);
    if (rc != 0) {
        SILK_ERROR("Failed to unmap stack area memory. errno=%d", errno);
        ret = SILK_STAT_STACK_FREE_FAILED;
    }
    return ret;
}

/*
 * allocate a silk instance to schedule new work
 *
 * Input
 * engine - the engine from which the silk is allocated & to which it is posted for execution.
 * entry_func - the function that will be executed by the silk instance.
 * ctx - a value that will be passed on to the entry_func (just like in pthread_create())
 *
 * Ouput
 * silk - the silk instance that was allocated.
 */ 
enum silk_status_e
silk_alloc(struct silk_engine_t   *engine,
           silk_uthread_func_t    entry_func,
           void                   *entry_func_arg,
           struct silk_t        **silk)
{
    struct silk_t  *s;
    enum silk_status_e   silk_stat;


    pthread_mutex_lock(&engine->mtx);
    // take a silk instance off the free list (if possible)
    if (likely(!SLIST_EMPTY(&engine->free_silks))) {
        s = SLIST_FIRST(&engine->free_silks);
        SLIST_REMOVE_HEAD(&engine->free_silks, next_free);
        engine->num_free_silk--;
        SILK_DEBUG("allocated Silk#%d. still %d available",
                   s->silk_id, engine->num_free_silk);
        assert(SILK_STATE(s) == SILK_STATE__FREE);
        // initialize new silk startup info
        s->entry_func = entry_func;
        s->entry_func_arg = entry_func_arg;
        silk__set_state(s, SILK_STATE__ALLOC);
        *silk = s;

        silk_stat = SILK_STAT_OK;
    } else {
        silk_stat = SILK_STAT_NO_FREE_SILK;
    }

    pthread_mutex_unlock(&engine->mtx);
    return silk_stat;
}

/*
 * dispatches an allocated silk to start running. the actual execution depends on the scheduler.
 */
enum silk_status_e
silk_dispatch(struct silk_engine_t   *engine,
              struct silk_t          *s)
{
    enum silk_status_e   silk_stat;

    SILK_DEBUG("dispatching Silk#%d", s->silk_id);
    assert(SILK_STATE(s) == SILK_STATE__ALLOC);
    silk_stat = silk_send_msg_code(engine, SILK_MSG_START, s->silk_id);

    return silk_stat;
}


/*
 * a set of silk_kill_*() API's which will cause the silk instance to stop running & become
 * free for new allocation.
 * if the call is made for the same silk that is killed, then the call will NOT return.
 * if the call is made for a different silk (than the caller) then:
 *    the call returns
 *    the killed silk will actually get killed only when it returns control to the scheduler
 *        (i.e.: dies, terminates, or yields()).
 */
enum silk_status_e
silk_eng_kill(struct silk_engine_t   *engine,
              struct silk_t          *silk)
{
    //struct silk_msg_t    msg;
    silk_id_t            my_id; // in case i'm in a silk context
    enum silk_status_e   silk_status;
    const uint32_t       silk_state = SILK_STATE(silk);


    SILK_DEBUG("killing Silk#%d", silk->silk_id);
    if (unlikely((silk_state == SILK_STATE__FREE) ||
                 (silk_state == SILK_STATE__TERM))) {
        SILK_DEBUG("silk %d is in state %d - no point killing it.",
                   silk->silk_id, silk_state);
        return SILK_STAT_OK;
    }
    if (silk_state == SILK_STATE__ALLOC) {
        SILK_DEBUG("silk %d recycled into free list", silk->silk_id);
        silk_eng_add_free_silk(engine, silk);
        return SILK_STAT_OK;
    }
    assert(silk_state == SILK_STATE__RUN);
    silk__set_state(silk, SILK_STATE__TERM);
    if (/*struct silk_execution_thread_t */silk__my_thread_obj()) {
        SILK_DEBUG("Silk#%d killed by silk thread", silk->silk_id);
        my_id = silk__my_id();
        if (my_id == silk->silk_id) {
            SILK_DEBUG("Silk#%d killing itself", silk->silk_id);
            silk_status = silk_send_msg_code(engine, SILK_MSG_TERM, silk->silk_id);
            /*
             * switch out so that the silk becomes inactive & can be recycled. we should
             * never return from this call. we will consume all of the msgs destined to
             * us until the MSG_TERM is poped & this will cause the scheduler to recycle
             *  this silk
             */
            silk_drain_msgs();
            // TODO: consider just calling silk_yield(&msg); instead of drain()
            assert(0);
        } else {
            silk_status = silk_send_msg_code(engine, SILK_MSG_TERM, silk->silk_id);
            /*
             * Nothing more to do bcz we have a single processing thread. this means that the 
             * other thread isnt running. we just need to wait for it to pop the TERM msg & recycle
             * itself.
             */
            SILK_DEBUG("Silk#%d killed by Silk#%d", silk->silk_id, my_id);
            silk_status = SILK_STAT_OK;
        }
    } else {
        SILK_DEBUG("Silk#%d killed by non silk thread", silk->silk_id);
        silk_status = silk_send_msg_code(engine, SILK_MSG_TERM, silk->silk_id);
    }
    return silk_status;
}
