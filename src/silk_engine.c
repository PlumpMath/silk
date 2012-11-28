/*
 * the context that is saved for a silk uthread when it is swapped-out.
 * Copyight (C) Eitan Ben-Amos, 2012
 */

#include <memory.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#define __USE_MISC
#include <sys/mman.h>


#include "silk.h"
 


enum silk_status_e
silk_msg_init(struct silk_incoming_msg_queue_t      *q)
{
    q->next_write = 0;
    q->next_read = 0;
    memset(q->msgs, 0, sizeof(q->msgs));
    return SILK_STAT_OK;
}

enum silk_status_e
silk_msg_terminate(struct silk_incoming_msg_queue_t      *q)
{
    return SILK_STAT_OK;
}

/*
 * move the index into the next slot
 */
static inline uint32_t
silk_msg_next_index(uint32_t       index)
{
    assert(index < MSG_QUEUE_SIZE);
    index ++;
    if (index == MSG_QUEUE_SIZE)
	index = 0;
    return index;
}
static inline bool
silk_msg_is_empty(struct silk_incoming_msg_queue_t      *q)
{
    if (q->next_write == q->next_read) {
	return true;
    } else {
	return false;
    }
}
static inline bool
silk_msg_is_full(struct silk_incoming_msg_queue_t      *q)
{
    if (silk_msg_next_index(q->next_write) == q->next_read) {
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
silk_msg_send(struct silk_incoming_msg_queue_t      *q,
	      struct cilk_msg_t                     *msg)
{
    if (silk_msg_is_full(q)) {
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
bool silk_msg_get_next(struct silk_incoming_msg_queue_t      *q,
		       struct cilk_msg_t                     *msg)
{
    if (silk_msg_is_empty(q)) {
	return false;
    }
    *msg = q->msgs[q->next_read];    
    q->next_read = silk_msg_next_index(q->next_read);
    return true;
}

/*
 * Information related to the Silk engine thread (e.g.: pthread, etc) which is used to actually execute the micro-threads
 */
struct silk_execution_thread_t {
  // The silk processing instance that this thread serves
  struct silk_engine_t               *engine;
  // The pthread ID that is used as our execution engine
  pthread_t                          id;
  // indicate when the thread should terminate itself.
  bool                               terminate;
};

 
/*
 * A processing engine with a single thread
 */
struct silk_engine_t {
    // the thread which actually runs all silks
    struct silk_execution_thread_t         exec_thr;
    // msgs which are pending processing
    struct silk_incoming_msg_queue_t       msg_sched;
    // the memory area used as stack for the uthreads
    void                                   *stack_addr;
    // the configuration we started with
    struct silk_engine_param_t             cfg;
};


/*
 * do a context switch between the uthread pointed
 * register EAX & ECX are defined such that the caller responsibility is to save & restore them – hence we don’t need to save/restore them as part of the context.
 *
 * BEWARE: inline assembly sometimes causes the compiler to generate wrong code. however, its better for performance bcz each function that calls for a context switch will have its own copy thereby allowing branch prediction to accurately predict the jumps. and it also save stack frame work :)
 *
 * Input:
 * first parameter is the destination stack, here its EAX register
 * first parameter is the destination stack, here its EBX register
 */
#if 0
/*static inline*/ void
silk_swap_stack_context(void***, void**) __attribute__((regparm(2)));
/*static inline*/ void
silk_swap_stack_context(void***, void**) /*__attribute__((regparm(2)))*/
{
    __asm__(
	    "pushl %ebp\n\t"
	    "pushl %ebx\n\t"
	    "pushl %esi\n\t"
	    "pushl %edi\n\t"
	    "movl  %esp, (%eax)\n\t"
	    "movl  %edx, %esp\n\t"
	    "popl  %edi\n\t"
	    "popl  %esi\n\t"
	    "popl  %ebx\n\t"
	    "popl  %ebp\n\t"
	    "ret\n\t"
	    );
}
#endif

 
/*
 * This is the entry function for a pthread that is used to execute uthreads.
 * we first switch into a uthread & then jump from one uthread to another WITHOUT ever going back to the pthread-provided stack (we might want that for IDLE callback processing)
 * when the engine is terminated, the thread will switch from the last uthread is executed to the original pthread stack frame (in which this function started to execute) & this function will then return & terminate the posix thread.
 */
static void *silk_thread_entry(void *ctx)
{
    struct silk_execution_thread_t         *exec_thr = (struct silk_execution_thread_t*)ctx;
    struct silk_engine_t                   *engine = exec_thr->engine;
    struct cilk_msg_t                      msg;

    do {
	// retrieve the next msg (based on priorities & any other application
	// specific rule) to be processed.
	if (silk_msg_get_next(&exec_thr->engine->msg_sched, &msg)) {
	    if (unlikely(msg.msg == SILK_TERM)) {
		SILK_INFO("thread %lu processing TERM msg", exec_thr->id);
		exec_thr->terminate = true;
	    } else {
		// switch into the uthread that is the msg destination
		assert(0); // DO IT !!!
	    }
	} else {
	    // IDLE processing
	    engine->cfg.idle_cb(exec_thr);
	}
    } while (likely(exec_thr->terminate == false));
    SILK_INFO("Thread exiting. id=%lu", exec_thr->id);
    return NULL;
}

enum silk_status_e
silk_thread_init (struct silk_execution_thread_t        *exec_thr,
		  struct silk_engine_t                  *engine)
{
    int          rc;
 
		
    exec_thr->engine = engine;
    exec_thr->terminate = false;
    rc = pthread_create(&exec_thr->id, NULL, silk_thread_entry, exec_thr);
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
	       struct cilk_msg_t                     *msg)
{
    enum silk_status_e    silk_stat;

    silk_stat = silk_msg_send(&engine->msg_sched, msg);
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
    struct cilk_msg_t        msg = {
	.msg = msg_code,
	.silk_id = silk_id,
    };
    return silk_send_msg (engine, &msg);
}
 
static inline enum silk_status_e
silk_eng_terminate (struct silk_execution_thread_t       *exec_thr)
{
    return silk_send_msg_code(exec_thr->engine, SILK_TERM, 0/* doesnt matter - its for the engine itself*/);
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
    int                    i, rc;
 

    if (param-> num_stack_pages < 1)
	return SILK_STAT_INVALID_STACK_SIZE;
    if (param-> num_silk < SILK_MIN_NUM_THREADS)
	return SILK_STAT_INVALID_NUM_SILK;

    memset(engine, 0, sizeof(*engine));
    memcpy(&engine->cfg, param, sizeof(engine->cfg));
    /*
     * allocate memory for stacks
     * memory layout would be
     * |stack for uthread 0 | unmapped (protection) pages | stack for uthread 1 | unmapped (protection) pages | ….
     * all memory is initially allocated as "No Access" & only the alowed 
     * areas will be allowed on top of that.
     */
    stack_size = SILK_SINGLE_STACK(param) * param->num_silk;
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
    engine->stack_addr = mmap(param->stack_addr, stack_size, PROT_NONE, 
			      mem_flags, -1 /* ignored*/, 0);
    if (engine->stack_addr == MAP_FAILED) {
	SILK_ERROR("Failed to allocate stack memory area. errno=%d", errno);
	ret = SILK_STAT_STACK_ALLOC_FAILED;
	goto stack_alloc_fail;
    }
    // TODO: set each Silk instance stack area to PROT_WRITE
    for (i=0, addr=engine->stack_addr;
	 i < param->num_silk;
	 i++, addr += SILK_SINGLE_STACK(param)) {
	rc = mprotect(addr, param->num_stack_pages * PAGE_SIZE, PROT_WRITE);
	if (rc != 0) {
	    SILK_ERROR("Failed to set stack memory protection. errno=%d", errno);
	    ret = SILK_STAT_STACK_PROTECTION_SCHEME_FAILED;
	    goto stack_prot_fail;
	}
    }

    // initialize the msg queue object
    ret = silk_msg_init(&engine->msg_sched);
    if (ret != SILK_STAT_OK) {
	goto msg_q_init_fail;
    }

    // let the thread execution start
    ret = silk_thread_init(&engine->exec_thr, engine);
    if (ret != SILK_STAT_OK) {
	goto thread_init_fail;
    }

    return SILK_STAT_OK;

 thread_init_fail:
    silk_msg_terminate(&engine->msg_sched);
 msg_q_init_fail:
 stack_prot_fail:
    rc = munmap(param->stack_addr, stack_size);
    if (rc != 0) {
	SILK_ERROR("Failed to unmap stack area memory. errno=%d", errno);
	/* were already in error handling path - continue as if no error */
    }
 stack_alloc_fail:

    return ret;
}

/*
 * notifies the engine to terminate itself (i.e.: stop processing & be ready for cleanup)
 */
enum silk_status_e
silk_terminate(struct silk_engine_t   *engine)
{
    silk_eng_terminate(&engine-> exec_thr);

    return SILK_STAT_OK;
}
 

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
    stack_size = SILK_SINGLE_STACK(cfg) * cfg->num_silk;
    rc = munmap(engine->stack_addr, stack_size);
    if (rc != 0) {
	SILK_ERROR("Failed to unmap stack area memory. errno=%d", errno);
	ret = SILK_STAT_STACK_FREE_FAILED;
    }
    return ret;
}

 

/*
  free all resources allocated to an engine.
            *make sure the engine has terminated !!!
*/
void
silk_destroy (struct silk_engine_t   *engine)
{
 

}

#include <unistd.h> 
static void
ping_pong_idle_cb (struct silk_execution_thread_t   *exec_thr)
{
    struct silk_engine_t                   *engine = exec_thr->engine;

    assert(engine->cfg.ctx == NULL);
    printf("ping pong idle\n");
    sleep(1); // sleep for 1 sec

    // TODO: replace with real code. for now just exit after some time
}

int main (int   argc, char **argv)
{
    struct silk_engine_param_t    silk_param = {
	.flags = 0,
	// TODO: make it possible to select an address!!!
	.stack_addr = (void*)NULL,
	//.stack_addr = (void*)0xb0000000,
	.num_stack_pages = 16,
	.num_stack_seperator_pages = 4,
	.num_silk = 1024,
	.idle_cb = ping_pong_idle_cb,
	.ctx = NULL,
    };
    struct silk_engine_t   engine;
    enum silk_status_e     silk_stat;

 

    printf("Initializing Silk engine...\n");
    silk_stat = silk_init(&engine, &silk_param);
    printf("Silk initialization returns:%d\n", silk_stat);
    sleep(10);
    silk_stat = silk_terminate(&engine);
    printf("Silk termination returns:%d\n", silk_stat);
    silk_stat = silk_join(&engine);
    printf("Silk join returns:%d\n", silk_stat);
}
