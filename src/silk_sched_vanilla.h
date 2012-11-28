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
 * 4) when terminated, will process the whole queue until it pops the SILK_MSG_TERM_THREAD msg
 *
 * Notes:
 * Some API's have both a locked & unlocked version. the unlocked has a 
 * preceding "_" but is otherwise identical
 *
 * 
 * TODO: the whole scheduler is here !!!
 * it should move into its own C file & some code should be generic for ANY scheduler.
 */

struct silk_incoming_msg_queue_t {
    // common info of all schedulers.
    struct silk_sched_base_t     base;
    // a mutex to guard any access to the queue
    pthread_mutex_t              mtx;
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
    pthread_mutex_init(&q->mtx, NULL);
    return SILK_STAT_OK;
}

/*
 * terminate a msg scheduler
 */
enum silk_status_e
silk_sched_terminate(struct silk_incoming_msg_queue_t      *q)
{
    pthread_mutex_destroy(&q->mtx);
    return SILK_STAT_OK;
}

/*
 * move the index into the next slot
 * BEWARE: queue must be locked
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

/*
 * BEWARE: queue must be locked
 */
static inline bool
_silk_sched_is_empty(struct silk_incoming_msg_queue_t      *q)
{
    if (q->next_write == q->next_read) {
	return true;
    } else {
	return false;
    }
}

static inline bool
silk_sched_is_empty(struct silk_incoming_msg_queue_t      *q)
{
    bool   ret;

    pthread_mutex_lock(&q->mtx);
    ret = silk_sched_is_empty(q);
    pthread_mutex_unlock(&q->mtx);
    return ret;
}
/*
 * BEWARE: queue must be locked
 */
static inline bool
_silk_sched_is_full(struct silk_incoming_msg_queue_t      *q)
{
    if (silk_sched_next_index(q->next_write) == q->next_read) {
	return true;
    } else {
	return false;
    }
}

static inline bool
silk_sched_is_full(struct silk_incoming_msg_queue_t      *q)
{
    bool   ret;

    pthread_mutex_lock(&q->mtx);
    ret = _silk_sched_is_full(q);
    pthread_mutex_unlock(&q->mtx);
    return ret;
}
/*
 * if queue isnt full, write the msg into the tail of the queue
 * internal Silk library API, for engine layer only
 */
enum silk_status_e
silk_sched_send(struct silk_incoming_msg_queue_t      *q,
                struct silk_msg_t                     *msg)
{
    enum silk_status_e   silk_stat;

    pthread_mutex_lock(&q->mtx);
    if (_silk_sched_is_full(q)) {
	silk_stat = SILK_STAT_Q_FULL;
	goto out;
    }
    q->msgs[q->next_write] = *msg;
    q->next_write++;
    silk_stat = SILK_STAT_OK;

 out:
    pthread_mutex_unlock(&q->mtx);
    return silk_stat;
}

/*
 * fetch the next msg to be processed, based on the scheduler scheduling decision
 * This is the place to implement various scheduling policies such as priority
 * queue, etc
 * return true when a msg is returned, false otherwise
 */
bool silk_sched_get_next(struct silk_incoming_msg_queue_t      *q,
                         struct silk_msg_t                     *msg)
{
    bool ret;

    pthread_mutex_lock(&q->mtx);
    if (_silk_sched_is_empty(q)) {
        ret = false;
        goto out;
    }
    *msg = q->msgs[q->next_read];    
    q->next_read = silk_sched_next_index(q->next_read);
    ret = true;

 out:
    pthread_mutex_unlock(&q->mtx);
    return ret;
}


#endif // __SILK_SCHED_H__
