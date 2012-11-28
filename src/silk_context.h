/*
 * Copyight (C) Eitan Ben-Amos, 2012
 */
#ifndef __SILK_CONTEXT_H__
#define __SILK_CONTEXT_H__

#include <stdint.h>
#include <unistd.h>

/*
 * Choose a context switch implementation
 * 1) libc based
 * This requires a syscall & hence misses much of the point of light-weight threads.
 * however, it has some important advantage:
 *     a) its simple - important for porting onto a new platform
 *     b) more portable (for oher *nix based platforms) or glibc based ones
 *     c) removes many unclear issues for initial development
 *
 * 2) assembly based, optimized for minimal state.
 *
 * TODO the below control macros should be used from a configure script.
 */
//#define SILK_CONTEXT__LIBC
#define SILK_CONTEXT__MINIMAL


/*
 * The silk micro-thread instance start routine.
 */
typedef void (*silk_uthread_func_t) (void);


#if defined (SILK_CONTEXT__LIBC)
/*
 * LIBC API's based context switch
 */
#define __USE_XOPEN_EXTENDED
#include <ucontext.h>

struct silk_exec_state_t {
    ucontext_t   libc_state;
};


#elif defined (SILK_CONTEXT__MINIMAL)

#if defined (__i386__)

/*
 * the context that is saved for a silk uthread when it is swapped-out.
 * for now we save the whole context on the stack, so just the ESP needs to be
 * here.
 * BEWARE: This structure has just one member but the assembly code which takes a pointer to this structure uses it as if its the pointer to the member !!! so if more members need to be added, they must NOT be before the "esp" member !!!
 */
struct silk_exec_state_t {
    void  *esp;
};



/*
 * Input:
 * initial_ctx - the initial context to be created
 * stack_buf - the buffer to hold the uthread stack.
 * size - the size (in bytes) of the stack buffer.
 */
void silk_create_initial_stack_context(struct silk_exec_state_t *initial_ctx,
                                       silk_uthread_func_t start_func,
                                       char     *stack_buf,
                                       size_t    size);

void silk_swap_stack_context(void    *switch_to_esp,
                             void   **switch_from_esp);

#elif defined (__x86_64__)

/*
 * the context that is saved for a silk uthread when it is swapped-out.
 */
struct silk_exec_state_t {
    void  *rbx;
    void  *rsp;
    void  *rbp;
    void  *r12;
    void  *r13;
    void  *r14;
    void  *r15;
};

void silk_create_initial_stack_context(struct silk_exec_state_t *initial_ctx,
                                       silk_uthread_func_t       start_func,
                                       char                     *stack_buf,
                                       size_t                    size);

void silk_swap_stack_context(struct silk_exec_state_t *switch_to, 
                             struct silk_exec_state_t *switch_from);


#endif // __x86_64__

#endif // SILK_CONTEXT__MINIMAL

#endif // __SILK_CONTEXT_H__
