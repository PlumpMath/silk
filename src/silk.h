#ifndef __SILK_H__
#define __SILK_H__

#include <stdint.h>
#include <stdbool.h>



// TODO: see if these have some std definition somewhere.
#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)

// TODO: platform adaptation facilities
#include <stdio.h>

#define SILK_ERROR(fmt, ...)   printf("ERR :" fmt "\n", ## __VA_ARGS__)
#define SILK_WARN(fmt, ...)    printf("WARN:" fmt "\n", ## __VA_ARGS__)
#define SILK_INFO(fmt, ...)    printf("INFO:" fmt "\n", ## __VA_ARGS__)

#define PAGE_SIZE    (4*1024)

/*
 * The size (in 4KB pages & in bytes) of a single Silk stack area (inc. separator)
 */
#define SILK_SINGLE_STACK_PG(param)   (param->num_stack_pages + param->num_stack_seperator_pages)
#define SILK_SINGLE_STACK(param) (SILK_SINGLE_STACK_PG(param) * PAGE_SIZE)

/*
 * The minimum number of uthread contexts we can work with
 */
#define SILK_MIN_NUM_THREADS                        2


/*
 * Forward declarations
 */
struct silk_execution_thread_t;
struct silk_engine_t;

/*
 * for now we save the whole context on the stack, so just the ESP needs to be
 * here
 */
struct silk_exec_state_t {
    void  *esp_register;
};


/*
 * various status/error codes
 */
enum silk_status_e {
    SILK_STAT_OK = 0,
    SILK_STAT_INVALID_STACK_SIZE,
    SILK_STAT_INVALID_NUM_SILK,
    SILK_STAT_THREAD_CREATE_FAILED,
    SILK_STAT_THREAD_ERROR,
    SILK_STAT_STACK_ALLOC_FAILED,
    SILK_STAT_STACK_FREE_FAILED,
    SILK_STAT_STACK_PROTECTION_SCHEME_FAILED,
    SILK_STAT_Q_FULL,
};

 
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
 * the message codes we can send to uthreads. A single byte should usually be enough.
 */
enum __attribute__((packed)) silk_msg_code_e {
    SILK_MSG_INVALID = 0,       // don’t use 0 as it might be easily caused by non-initialized stuff
    SILK_START,               // instruct the uthread to start running.
    SILK_TERM,                // instructs the uthread to terminate
    SILK_MSG_CODE_LAST,       // the last valid of msg codes used by the Silk library.

    // msg code range available for the application that uses the Silk library
    SILK_APP_CODE_FIRST = 1000,
};


/*
 * encapsulate a message that is sent to a silk micro-thread
 */
struct cilk_msg_t {
  // additional information that the sender attached to the msg
  void*                       ctx;
  // the index into the uthread array identifying the target uthread to process this message
  uint16_t                    silk_id;
  enum silk_msg_code_e        msg;
};

 
/*
 * a single silk uthread instance
 */
struct silk_context_t {
  // The silk processing instance that this thread serves
  //struct silk_engine_t        engine;
  // the context saved during the last run.
  struct silk_exec_state_t      exec_state;
};


/*
 * A queue of messages pending processing.
 * This object is bound to have multiple implementations such as.
 * 1) simple linear msg queue
 * 2) priority based msg queue
 * 3) a queue with application-specific decision logic
 */
struct silk_incoming_msg_queue_t {
    // a fixed number of msgs in a simple msg queue.
#define MSG_QUEUE_SIZE           8*1024 // msgs
    struct cilk_msg_t            msgs[MSG_QUEUE_SIZE];
    // The read/write indices into the msg vector
    uint32_t                     next_write;
    uint32_t                     next_read;
};

/******************************************************************************
 * Prototypes
 ******************************************************************************/

#endif // __SILK_H__
