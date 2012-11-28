/*
 * Copyight (C) Eitan Ben-Amos, 2012
 */
#ifndef __SILK_SCHED_VANILLA_H__
#define __SILK_SCHED_VANILLAH__


/*
 * The first scheduler is a very basic one with which we develop the core library. it is:
 * 1) fixed size for msg instance & the whole msg queue.
 * 2) queue access requires a lock
 * 3) queue is processed with strict order of FIFO
 * 4) when terminated, will process the whole queue until it pops the SILK_MSG_TERM msg
 */


struct silk_incoming_msg_queue_t {
    // a fixed number of msgs in a simple msg queue.
#define MSG_QUEUE_SIZE           8*1024 // msgs
    struct silk_msg_t            msgs[MSG_QUEUE_SIZE];
    // The read/write indices into the msg vector
    uint32_t                     next_write;
    uint32_t                     next_read;
};


enum silk_status_e
silk_sched_init(struct silk_incoming_msg_queue_t      *q)
{
    q->next_write = 0;
    q->next_read = 0;
    memset(q->msgs, 0, sizeof(q->msgs));
    return SILK_STAT_OK;
}

/*
 * terminate a msg scheduler
 */
enum silk_status_e
silk_sched_terminate(struct silk_incoming_msg_queue_t      *q)
{
    return SILK_STAT_OK;
}

/*
 * move the index into the next slot
 */
static inline uint32_t
silk_sched_next_index(uint32_t       index)
{
    assert(index < MSG_QUEUE_SIZE);
    index ++;
    if (index == MSG_QUEUE_SIZE)
	index = 0;
    return index;
}
static inline bool
silk_sched_is_empty(struct silk_incoming_msg_queue_t      *q)
{
    if (q->next_write == q->next_read) {
	return true;
    } else {
	return false;
    }
}
static inline bool
silk_sched_is_full(struct silk_incoming_msg_queue_t      *q)
{
    if (silk_sched_next_index(q->next_write) == q->next_read) {
	return true;
    } else {
	return false;
    }
}
/*
 * if queue isnt full, write the msg into the tail of the queue
 * internal Silk library API, for engine layer only
 */
enum silk_status_e
silk_sched_send(struct silk_incoming_msg_queue_t      *q,
		struct silk_msg_t                     *msg)
{
    if (silk_sched_is_full(q)) {
	return SILK_STAT_Q_FULL;
    }
    q->msgs[q->next_write] = *msg;
    q->next_write++;
    return SILK_STAT_OK;
}

/*
 * fetch the next msg to be processed, based on the scheduler scheduling decision
 * This is the place to implement various scheduling policies such as priority queue, etc
 * return true when a msg is returned, false otherwise
 */
bool silk_sched_get_next(struct silk_incoming_msg_queue_t      *q,
			 struct silk_msg_t                     *msg)
{
    if (silk_sched_is_empty(q)) {
	return false;
    }
    *msg = q->msgs[q->next_read];    
    q->next_read = silk_sched_next_index(q->next_read);
    return true;
}


#endif // __SILK_SCHED_H__
