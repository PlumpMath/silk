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
 * This is the Silk ID of the instance which is used to start running when the engine comes up
 */
#define SILK_INITIAL_ID   0

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
    do {
        //TODO: need to remove return value
        if (silk_yield(&msg)) {
            switch (msg.msg) {
            case SILK_MSG_BOOT:
                SILK_DEBUG("Silk#%d processing BOOT msg", s->silk_id);
                assert(SILK_STATE(s) == SILK_STATE__BOOT);
                silk__set_state(s, SILK_STATE__FREE);
                break;

            case SILK_MSG_START:
                s = silk_get_ctrl_from_id(engine, msg.silk_id);
                assert(SILK_STATE(s) == SILK_STATE__ALLOC);
                SILK_INFO("silk %d starting", silk__my_id());
                silk__set_state(s, SILK_STATE__RUN);
                s->entry_func(s->entry_func_arg);
                assert(SILK_STATE(s) == SILK_STATE__RUN);
                silk__set_state(s, SILK_STATE__TERM);
                SILK_INFO("silk %d ended", silk__my_id());
                break;

            case SILK_MSG_TERM:
                SILK_INFO("silk thread %lu:%d processing TERM msg", 
                          exec_thr->id, msg.silk_id);
                /*
                 * BEWARE: we push the silk on which we currently execute to the free list.
                 * This is OK as long as the addition of the silk to the free list is
                 * protected by a lock so no other thread can allocate it & fire it while
                 * we use its stack.
                 */
                break;

            case SILK_MSG_TERM_THREAD:
                SILK_INFO("kernel thread %lu processing TERM msg", exec_thr->id);
                engine->terminate = true;
                break;

            default:
                // switch into the uthread that is the msg destination
                assert(0); // DO IT !!!
            }
        } else {
            // IDLE processing
            engine->cfg.idle_cb(exec_thr);
        }
    } while (likely(engine->terminate == false));
    SILK_INFO("thread %lu switching back to pthread stack", exec_thr->id);
#if defined (__i386__)
    silk_swap_stack_context(exec_thr->exec_state.esp, &s->exec_state.esp);
#elif defined (__x86_64__)
#error "not implemented"
    // it should be of the form:
    silk_swap_stack_context(&exec_thr->exec_state, &s->exec_state);
#endif
}

/*
 * This is the entry function for a pthread that is used to execute uthreads.
 * we first switch into a uthread & then jump from one uthread to another WITHOUT ever going back to the pthread-provided stack (we might want that for IDLE callback processing)
 * when the engine is terminated, the thread will switch from the last uthread is executed to the original pthread stack frame (in which this function started to execute) & this function will then return & terminate the posix thread.
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
     * p.s.
     * one might want to consider calling the IDLE function using the pthread stack.
     * but this implies a little extra overhead.
     * BEWARE: we are swapping into a silk which is designated "free" & use its stack
     * for our execution until we switch into a silk for processng its msg.
     */
    s = silk_get_ctrl_from_id(engine, silk_id);
#if defined (__i386__)
    silk_swap_stack_context(s->exec_state.esp, &exec_thr->exec_state.esp);
#elif defined (__x86_64__)
#error "not implemented"
    // it should be of the form:
    silk_swap_stack_context(&s->exec_state, &exec_thr->exec_state);
#endif
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
 * send a msg object into the engine msg queue
 */
static inline enum silk_status_e
silk_send_msg (struct silk_engine_t                  *engine,
               struct silk_msg_t                     *msg)
{
    enum silk_status_e    silk_stat;

    silk_stat = silk_sched_send(&engine->msg_sched, msg);
    return silk_stat;
}

/*
 * send a msg code into the engine msg queue
 */
static inline enum silk_status_e
silk_send_msg_code (struct silk_engine_t                  *engine,
                    enum silk_msg_code_e                  msg_code,
                    uint32_t                              silk_id)
{
    struct silk_msg_t        msg = {
        .msg = msg_code,
        .silk_id = silk_id,
    };
    return silk_send_msg (engine, &msg);
}
 
static inline enum silk_status_e
silk_eng_terminate (struct silk_execution_thread_t       *exec_thr)
{
    return silk_send_msg_code(exec_thr->engine, SILK_MSG_TERM_THREAD, 0/* doesnt matter - its for the engine itself*/);
}
 

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
    pthread_mutex_init(&engine->mtx,NULL);

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
        silk__set_state(&engine->silks[i], SILK_STATE__BOOT);
        engine->silks[i].silk_id = i;
        assert(addr == silk_get_stack_from_id(engine, i));
        silk_create_initial_stack_context(&s->exec_state,
                                          silk__main,
                                          addr,//silk_get_stack_from_id(engine, msg.silk_id),
                                          SILK_USEABLE_STACK(&engine->cfg));
        /*
         * ask each silk to boot into the place they all wait for msgs
         * The first msg causes the scheduler to switch into it. it then runs from the 
         * beggining os silk__main() up to the first silk_yield() where it joins all
         * silks awaiting control msgs. however, the msg itself wasnt processed bcz
         * it was lost when we switched into silk_main() which isnt built to retrieve
         * the msg which cause the context-switch into it.
         * the second msg is received by each silk instance & causes it to return from
         * the call to silk_yield(). this one is required only bcz the silk instance
         * state-machine requires a BOOT msg in order to change its state to FREE.
         * The second msg isnt sent to the INITIAL silk bcz this one executed its part
         * from silk_main() to silk_yield() as part of the pthread initialization. the
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
    
    // let the thread execution start
    ret = silk_thread_init(&engine->exec_thr, engine);
    if (ret != SILK_STAT_OK) {
        goto thread_init_fail;
    }

    // wait until all BOOT msgs are processed.
    while (!silk_sched_is_empty(&engine->msg_sched)) {
        SILK_DEBUG("waiting for all BOOT msgs to be processed");
        usleep(10);
    }
    SILK_DEBUG("All Silks completed booting !");

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
*/
bool silk_yield(struct silk_msg_t   *msg)
{
    struct silk_execution_thread_t *exec_thr = silk__my_thread_obj();
    struct silk_engine_t           *engine = exec_thr->engine;
    struct silk_t                  *s = silk__my_ctrl();
    struct silk_t                  *silk_trgt;
    silk_id_t                       msg_silk_id;
    

    // retrieve the next msg (based on priorities & any other application
    // specific rule) to be processed.
    if (silk_sched_get_next(&exec_thr->engine->msg_sched, &exec_thr->last_msg)) {
        /*
         * swap context into the silk which received the msg.
         * BEWARE: 
         * 1) 's' was set in previous loop iteration & hence it has the older silk 
         * object, which executed the last msg
         * 2) we must  optimize the case where we can skip switching from one instance to itself.
         * otherwise, we'll send the "current" stack-pointer as target & save a 
         * stack-pointer which is pushed into while saving the state. it will cause
         * us to return on the stack as if "before" we saved the state. very bad !!!
         */
        msg_silk_id = exec_thr->last_msg.silk_id;
        if (likely(s->silk_id != msg_silk_id)) {
            silk_trgt = &engine->silks[msg_silk_id];
            assert(silk_trgt->silk_id == msg_silk_id);
            SILK_DEBUG("switching from Silk#%d to Silk#%d", s->silk_id, silk_trgt->silk_id);
            SILK_SWITCH(silk_trgt, s);
            SILK_DEBUG("switched into Silk#%d", s->silk_id);
        }
        memcpy(msg, &exec_thr->last_msg, sizeof(*msg));
        return true;
    }
    return false;
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
    pthread_mutex_lock(&engine->mtx);
    // TODO: alloc a Silk instance
    static int silk_id = 0;
    struct silk_t  *s;
    enum silk_status_e   silk_stat;


    // TODO: for now were sure we have a free silk :)
    assert(silk_id < engine->cfg.num_silk);
    silk_stat = SILK_STAT_OK;


    s = &engine->silks[silk_id];

    assert(SILK_STATE(s) == SILK_STATE__FREE);
    s->entry_func = entry_func;
    s->entry_func_arg = entry_func_arg;
    silk__set_state(s, SILK_STATE__ALLOC);
    *silk = s;

    silk_id++;
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

    assert(SILK_STATE(s) == SILK_STATE__ALLOC);
    silk_stat = silk_send_msg_code(engine, SILK_MSG_START, s->silk_id);

    return silk_stat;
}


