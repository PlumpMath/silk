/*
 * Copyight (C) Eitan Ben-Amos, 2012
 */

#include "silk_tls.h"


#ifdef SILK_TLS__THREAD_SPECIFIC

#else // SILK_TLS__THREAD_SPECIFIC

/*
 * GCC TLS objects
 * Note that the below definition causes EVERY thread within the process that linked
 * the Silk library to have these members. so dont make it too big as there might be
 * many threads.
 */
__thread   void *silk_tls[SILK_TLS__MAX];

#endif // SILK_TLS__THREAD_SPECIFIC
