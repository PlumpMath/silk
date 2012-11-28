/*
 * Copyight (C) Eitan Ben-Amos, 2012
 * 
 * Base stuff for the Silk coroutines library
 */
#ifndef __SILK_BASE_H__
#define __SILK_BASE_H__


// TODO: see if these have some std definition somewhere.
#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)

/*
 * Logging facility
 */
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
 * the message codes we can send to uthreads. A single byte should usually be enough.
 */
enum __attribute__((packed)) silk_msg_code_e {
    SILK_MSG_INVALID = 0,       // don’t use 0 as it might be easily caused by non-initialized stuff
    SILK_MSG_START,               // instruct the uthread to start running.
    SILK_MSG_TERM,                // instructs the uthread to terminate
    SILK_MSG_CODE_LAST,       // the last valid of msg codes used by the Silk library.

    // msg code range available for the application that uses the Silk library
    SILK_MSG_APP_CODE_FIRST = 1000,
};


/*
 * encapsulate a message that is sent to a silk micro-thread
 * make usre we dont make it too big.
 */
struct silk_msg_t {
  // additional information that the sender attached to the msg
  void*                       ctx;
  // the index into the uthread array identifying the target uthread to process this message
  uint16_t                    silk_id;
  enum silk_msg_code_e        msg;
};

 

#endif // __SILK_BASE_H__
