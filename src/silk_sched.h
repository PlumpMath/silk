/*
 * Copyight (C) Eitan Ben-Amos, 2012
 */
#ifndef __SILK_SCHED_H__
#define __SILK_SCHED_H__

/*
 * A queue of messages pending processing.
 * This object is bound to have multiple implementations such as.
 * 1) simple linear msg queue
 * 2) a highly efficient multi-processor msg queue.
 * 3) priority based msg queue
 * 4) a queue with application-specific decision logic (e.g.: select next silk 
 *    based on various parameters rather than by the msg priority).
 */

/*
 * common info of all schedulers.
 */
struct silk_sched_base_t {
    void *dummy;// TODO: consider removing this structure if no use
};


#include "silk_sched_vanilla.h"

#endif // __SILK_SCHED_H__
