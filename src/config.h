/*
 * This header should later on be auto-generated using Gnu build tools.
 * for now i'm developing it myslef.
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__


/* enables context-switch based on LIBC API's. */
//#define SILK_CONTEXT__LIBC

/* enables context-switch based on internal assembly code */
#define SILK_CONTEXT__MINIMAL

/*
 * select a TLS implementation, whether pthreads or compiler support.
 */
//#define SILK_TLS__THREAD_SPECIFIC


#endif // __CONFIG_H__
