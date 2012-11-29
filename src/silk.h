/*
 * Copyight (C) Eitan Ben-Amos, 2012
 */
#ifndef __SILK_H__
#define __SILK_H__

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/queue.h>
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
    uint32_t             num_silk;
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
 *               | entry function returns  | FREE
 * TERM          | process recycle msg     | FREE
 */
#define SILK_STATE__FIRST   0x0
#define SILK_STATE__BOOT    0x0 // the silk boots (i.e.: hasnt reached the point it can process msg)
#define SILK_STATE__FREE    0x1 // free for anyone to allocate
#define SILK_STATE__ALLOC   0x2 // allocated but hasnt started running yet
#define SILK_STATE__RUN     0x3 // running, not terminated/exited yet
#define SILK_STATE__TERM    0x4 // actively running but terminated. once it calls silk_yield() it will be recycled & return to the free pool
#define SILK_STATE__LAST    0x4
#define SILK_STATE__MASK    0x7 // MASK to extract the state bits

// extract the state of a silk instance
#define SILK_STATE(s)   ((s)->state & SILK_STATE__MASK)


/*
 * a single silk uthread instance
 */
struct silk_t {
    // the link-list chaining object
    SLIST_ENTRY(silk_t)           next_free;
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
     * the silk is either transitions from FREE->ALLOC->RUN->TERM->FREE & at each
     * point only ne can touch it so no need to lock access.
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
 * definitions for BSD based linked list of silks (struct silk_t)
*/
SLIST_HEAD(silk_head_t, silk_t);

 
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
    // a list of free silk objects
    struct silk_head_t                     free_silks;
    // indicate when the thread should terminate itself.
    bool                                   terminate;
    // The number of Silks in free state
    uint32_t                               num_free_silk;
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
 */
static inline silk_id_t
silk__my_id()
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

static inline struct silk_t *
silk_id_to_ctrl(silk_id_t    silk_id)
{
    struct silk_execution_thread_t *exec_thr = silk__my_thread_obj();
    struct silk_engine_t           *engine = exec_thr->engine;
    return &engine->silks[silk_id];
}

void silk_yield(struct silk_msg_t   *msg);

enum silk_status_e
silk_init (struct silk_engine_t               *engine,
           const struct silk_engine_param_t   *param);

enum silk_status_e
silk_terminate(struct silk_engine_t   *engine);

enum silk_status_e
silk_join(struct silk_engine_t   *engine);

enum silk_status_e
silk_alloc(struct silk_engine_t   *engine,
           silk_uthread_func_t    entry_func,
           void                   *entry_func_arg,
           struct silk_t        **silk);

enum silk_status_e
silk_dispatch(struct silk_engine_t   *engine,
              struct silk_t          *s);


/*
 * allows any thread (rather than only a silk instance) to kill a silk instance.
 */
enum silk_status_e
silk_eng_kill(struct silk_engine_t   *engine,
              struct silk_t          *silk);

/*
 * allows a silk instance to kill any silk instance, whether itself or not.
 */
static inline enum silk_status_e
silk_kill(struct silk_t   *silk)
{
    struct silk_execution_thread_t         *exec_thr = silk__my_thread_obj();
    struct silk_engine_t                   *engine = exec_thr->engine;

    return silk_eng_kill(engine, silk);
}

/*
 * allows a silk instance to kill any silk instance, whether itself or not.
 */
static inline enum silk_status_e
silk_kill_id(silk_id_t      silk_id)
{
    return silk_kill(silk_id_to_ctrl(silk_id));
}


/*
 * send a msg object into the engine msg queue
 */
static inline enum silk_status_e
silk_send_msg (struct silk_engine_t                  *engine,
               struct silk_msg_t                     *msg)
{
    enum silk_status_e    silk_stat;

    SILK_DEBUG("send msg={code=%d, id=%d, ctx=%p}", msg->msg, msg->silk_id, msg->ctx);
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

/*
 * query the number of free silks (i.e.: ready to be allocated)
 */
static inline uint32_t
silk_eng__get_free_silks(struct silk_engine_t    *engine)
{
    uint32_t   num_free;

    pthread_mutex_lock(&engine->mtx);
    num_free = engine->num_free_silk;
    pthread_mutex_unlock(&engine->mtx);

    return num_free;
}

#endif // __SILK_H__
