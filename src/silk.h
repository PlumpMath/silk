/*
 * Copyight (C) Eitan Ben-Amos, 2012
 */
#ifndef __SILK_H__
#define __SILK_H__

#include <stdint.h>
#include <stdbool.h>
#include "silk_base.h"
#include "silk_tls.h"
#include "silk_sched.h"
#include "silk_context.h"






/*
 * Forward declarations
 */
struct silk_execution_thread_t;
struct silk_engine_t;



 
/*
 * a callback function to be provided by the application to be called when the
 * Silk engine has no more msgs to process.
 * This is the place to:
 * 1) do low priority work that needs to harvest idle CPU cycles
 * 2) go do expensive work that will provide new work (e.g.: a network 
 * application server might check all active sockets & read all the data that is
 * pending on them, pushing it as msgs into the queue for later processing by the
 * uthread, assuming each connection has its own uthread)
 * 3)
 */
typedef void (*silk_engine_idle_callback_t) (struct silk_execution_thread_t   *silk_thread);

/*
 * information to create a Silk processing entity.
 */
struct silk_engine_param_t {
    // flags controling various behaviors
    int32_t              flags;
#define SILK_CFG_FLAG_LOCK_STACK_MEM    0x01
    // Initial address of the stack area. set NULL for the library to provide
    void                 *stack_addr;
    // The number of 4KB pages for a stack of each silk.
    uint32_t             num_stack_pages;
    // The number if 4KB pages to separate between consecutive silk stack buffers
    uint32_t             num_stack_seperator_pages;
    // The number of silk instance to create
    unsigned             num_silk;
    // the callback function to be called when the engine has nothing to do (i.e.: no msgs to process)
    silk_engine_idle_callback_t         idle_cb;
    // a context to be attached by the application to the Silk execution object
    void                                *ctx;
};

/*
 * The states at which a silk object can be
 * current state | operation               | new state
 *-----------------------------------------------------------
 * BOOT          | process SILK_MSG_BOOT   | FREE
 * FREE          | silk_alloc()            | ALLOC
 * ALLOC         | process MSG_START       | RUN
 * RUN           | silk__exit()            | TERM
 *               | entry function returns  | TERM
 * TERM          | process recycle msg     | FREE
 */
#define SILK_STATE__FIRST   0x0
#define SILK_STATE__BOOT    0x0 // the silk boots (i.e.: hasnt reached the point it can process msg)
#define SILK_STATE__FREE    0x1 // free for anyone to allocate
#define SILK_STATE__ALLOC   0x2 // allocated but hasnt started running yet
// do we need SILK_STATE_IGNITE ?
#define SILK_STATE__RUN     0x3 // running, not terminated/exited yet
#define SILK_STATE__TERM    0x4 // terminated, pending recycle to make it FREE
#define SILK_STATE__LAST    0x4
#define SILK_STATE__MASK    0x7 // MASK to clear everything but the state bits

// extract the state of a silk instance
#define SILK_STATE(s)   ((s)->state & SILK_STATE__MASK)


/*
 * a single silk uthread instance
 */
struct silk_t {
    // the entry function of the silk thread
    silk_uthread_func_t           entry_func;
    // an argument to be passed to the silk entry point
    void                         *entry_func_arg;
    // the context saved during the last run.
    struct silk_exec_state_t      exec_state;
    // The silk processing instance that this thread serves
    //struct silk_engine_t        engine;
    /*
     * the state of this silk
     * 2 bits : current state of silk object
     */
    uint32_t                      state;
    // the unique silk_id of this instance
    silk_id_t                     silk_id;
};

static inline void 
silk__set_state(struct silk_t   *s,
                uint32_t         new_state)
{
    assert((new_state >= SILK_STATE__FIRST) && (new_state <= SILK_STATE__LAST));
    s->state = (s->state & ~SILK_STATE__MASK) | new_state;
}


/*
 * Information related to the Silk engine thread (e.g.: pthread, etc) which is used to 
 * actually execute the micro-threads
 */
struct silk_execution_thread_t {
    // The silk processing instance that this thread serves
    struct silk_engine_t               *engine;
    // The pthread ID that is used as our execution engine
    pthread_t                          id;
    /*
     * The silk context maintained for the pthread, when we switch from
     * the pthread to the first silk. we'll use it back only when we terminate
     * the engine, when the processing thread needs to terminate
     */
    struct silk_exec_state_t           exec_state;
    // the current msg we are reading or that is being processed
    struct silk_msg_t                  last_msg;
};

 
/*
 * A processing engine with a single thread
 */
struct silk_engine_t {
    // a mutex to guard access to engine state
    pthread_mutex_t                        mtx;
    // the thread which actually runs all silks
    struct silk_execution_thread_t         exec_thr;
    // msgs which are pending processing
    struct silk_incoming_msg_queue_t       msg_sched;
    // the memory area used as stack for the uthreads
    void                                   *stack_addr;
    // the state of each silk instance, inc. the context-switch
    struct silk_t                          *silks;
    // the configuration we started with
    struct silk_engine_param_t             cfg;
    // indicate when the thread should terminate itself.
    bool                                   terminate;
};

// verify that a silk ID is valid.
#define SILK_ASSERT_ID(engine, silk_id)   assert((silk_id) < (engine)->cfg.num_silk)

static inline void *
silk_get_stack_from_id(struct silk_engine_t   *engine,
                       silk_id_t              silk_id)
{
    SILK_ASSERT_ID(engine, silk_id);
    return engine->stack_addr + silk_id * SILK_PADDED_STACK(&engine->cfg);
}

static inline struct silk_t *
silk_get_ctrl_from_id(struct silk_engine_t   *engine,
                      silk_id_t              silk_id)
{
    SILK_ASSERT_ID(engine, silk_id);
    return engine->silks + silk_id;
}




/******************************************************************************
 * Prototypes
 ******************************************************************************/

/*
 * returns the silk id of the caller
 * we use the stack address to find the silk instance
 * TODO: test this API. just write a program to allocate all silks & execute each to verify its own ID
 */
silk_id_t silk__my_id()
{
    struct silk_execution_thread_t *exec_thr = silk__my_thread_obj();
    struct silk_engine_t           *engine = exec_thr->engine;
    uintptr_t   stk_start = (uintptr_t)engine->stack_addr;
    size_t      stack_size = SILK_PADDED_STACK(&engine->cfg) * engine->cfg.num_silk;
    uintptr_t   stk_end = (uintptr_t)(engine->stack_addr) + stack_size;
    // take the address of any stack variable
    uintptr_t   stk_addr = (uintptr_t)&engine;
    silk_id_t   silk_id;


    assert((stk_addr >= stk_start) && (stk_addr < stk_end));
    silk_id = (stk_addr - stk_start) / SILK_PADDED_STACK(&engine->cfg);
    return silk_id;
}

/*
 * returns the control-object for the silk making the call.
*/
static inline struct silk_t *
silk__my_ctrl()
{
    struct silk_execution_thread_t *exec_thr = silk__my_thread_obj();
    struct silk_engine_t           *engine = exec_thr->engine;
    const silk_id_t                 my_silk_id = silk__my_id();
    return &engine->silks[my_silk_id];
}
/*
 * This API allows the scheduler to take the calling Silk out-of-execution & switch 
 * to another silk instance. the specifics of such a decision is scheduler-specific.
 * When the function returns, the msg which awoke the silk is returned so it can be 
 * processed
*/
//TODO: make this return "void"!!!
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

#endif // __SILK_H__
