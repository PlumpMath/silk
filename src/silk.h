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
 * a unique integer identifying the silk instance.
 */
typedef uint32_t   silk_id_t;

 
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
 * FREE          | silk_alloc()            | ALLOC
 * ALLOC         | process MSG_START       | RUN
 * RUN           | silk__exit()            | TERM
 *               | entry function returns  | TERM
 * TERM          | process recycle msg     | FREE
 */
#define SILK_STATE__FIRST   0x0
#define SILK_STATE__FREE    0x0 // free for anyone to allocate
#define SILK_STATE__ALLOC   0x1 // allocated but hasnt started running yet
#define SILK_STATE__RUN     0x2 // running, not terminated/exited yet
#define SILK_STATE__TERM    0x3 // terminated, pending recycle to make it FREE
#define SILK_STATE__LAST    0x3
#define SILK_STATE__MASK    0x3 // MASK to clear everything but the state bits

// extract the state of a silk instance
#define SILK_STATE(s)   ((s)->state & SILK_STATE__MASK)


/*
 * a single silk uthread instance
 */
struct silk_t {
    // the entry function of the silk thread
    silk_uthread_func_t           entry_func;
    // the context saved during the last run.
    struct silk_exec_state_t      exec_state;
    // The silk processing instance that this thread serves
    //struct silk_engine_t        engine;
    /*
     * the state of this silk
     * 2 bits : current state of silk object
     */
    uint32_t                      state;
};

static inline void 
silk__set_state(struct silk_t   *s,
                uint32_t         new_state)
{
    assert((new_state >= SILK_STATE__FIRST) && (new_state <= SILK_STATE__LAST));
    s->state = (s->state & ~SILK_STATE__MASK) | new_state;
}


/******************************************************************************
 * Prototypes
 ******************************************************************************/

/*
 * returns the silk id of the caller
 */
silk_id_t silk__my_id()
{
    // TODO: implement this based on TLS of engine object
    return 0;
}

#endif // __SILK_H__
