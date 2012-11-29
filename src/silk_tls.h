/*
 * Copyight (C) Eitan Ben-Amos, 2012
 *
 * The purpose of this module is to provide Thread Local Storage encapsulation.
 * at first, i tried pthread_{set,get}specific() but it didnt work well when i played with the stack.
 * so this module separates the TLS requirements to allow easy migration to any platform 
 */
#ifndef __SILK_TLS_H__
#define __SILK_TLS_H__

#include "config.h"
#include <assert.h>

/*
 * The various TLS object we need to maintain
 */
enum silk_tls_id_e {
    /*
     * allows each pthread to retreive its own thread object which executes the silk instances.
     */
    SILK_TLS__THREAD_OBJ,
    // The last index, which also indicates the length of the required array
    SILK_TLS__MAX,
};


#ifdef SILK_TLS__THREAD_SPECIFIC

void silk__set_tls(int  index, const void *p)
{
     pthread_setspecific(index, p);
}
    
void *silk__get_tls(int  index)
{
    return pthread_getspecific(index);
}

#else

extern __thread   void *silk_tls[SILK_TLS__MAX];

/*
 * when called by a thread which executes silks, it will return the thread object that 
 * matches the pthread which is calling.
 *
 * Notes:
 * Attempting this with pthread_{get,set}specific() fails.
 */
static inline void
silk__set_tls(enum silk_tls_id_e  id, void *p)
{
    assert((id >= 0) && (id < SILK_TLS__MAX));
    silk_tls[id] = p;
}

static inline void *
silk__get_tls(enum silk_tls_id_e  id)
{
    assert((id >= 0) && (id < SILK_TLS__MAX));
    return silk_tls[id];
}
#endif

/*
 * retrive the thread object that matches the pthread which is calling.
 * Assumes the thread is an execution engine of silks
 */
static inline struct silk_execution_thread_t *
silk__my_thread_obj ()
{
    return silk__get_tls(SILK_TLS__THREAD_OBJ);
}

#endif // __SILK_TLS_H__
