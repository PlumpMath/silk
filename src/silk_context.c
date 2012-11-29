/*
 * Copyight (C) Eitan Ben-Amos, 2012
 */
#include "config.h"
#include <assert.h>
#include "silk_context.h"



#if defined (SILK_CONTEXT__LIBC)
/*
void makecontext(ucontext_t *ucp, void (*func)(), int argc, ...);
int swapcontext(ucontext_t *oucp, ucontext_t *ucp);
*/

#elif defined (SILK_CONTEXT__MINIMAL)


/*
 * Important x86 (32/64 bit) notes
 * X86 has 8 registers (E{AX, BX, CX, DX, SI, DI, BP, SP} while x86-64 has 16 registers (R0 .. R15]
 * of which the first 8 are the same as the old ones (EAX, …) while the higher 8 are new & are
 * named only in this way (i.e.: R8 .. R15)
 * The registers naming convention is like so:
 * REX indicate word size of an opcode:
 *               R = 64-bit (e.g.: RAX)
 *               E = 32-bit (e.g.: EAX)
 *               <NONE> = 16 bit (e.g.: AX)
 *               L = 8 bit (e.g.: AL)
 *               RA{b,w,d,<none>} can be used to uniformly name a register with a eidth of
 *                  {8,16,32,64} bit respectively
 * See full register naming table @ http://msdn.microsoft.com/en-us/library/windows/hardware/ff561499(v=vs.85).aspx
 * The x86-64 also has:
 *         Eight 80-bit x87 registers (floating point).
 *         Eight 64-bit MMX registers. (These overlap with the x87 registers.)
 *         The original set of eight 128-bit SSE registers is increased to sixteen.
 *         AVX register (256-bit wide)
 * different applications might require different registers to be saved in the context.
 * Always make sure we have enough space for the Red Zone (128 byte with GCC)
 *
 *
 * For System V sytems (Gnu/Linux, Solaris, FreeBSD, etc) the ABI states that:
 * [IA-32]
 *     1) EBX, ESI, EDI registers are “callee-saved” – callee mustn’t change these. If it changes
 *        these than it must restore them to old values before returning.
 *     2) EAX, EDX, ECX registers are “caller-saved” – caller saves these if it wants to preserve
 *        them after a function-call retuns.
 *     3) Using the “fastcall” calling convention, GCC places the first 2 function parameters in
 *        registers ECX (first) & EDX (second).
 *     4) Using the standard calling convention, ALL parameters are on the stack. They are pushed
 *        in reversed order from their order in the argument list. Furthermore, all parameters are
 *        multiples of DWORD (32-bit) size.
 *     5) example for accessing function arguments in the caller for the function:
 *        “int fee(int fie, char foe, double fum)” is :
 *        i) use the EBP with an offset equal to 4 * (n + 2), where n is the number of the parameter
 *           in the argument list (not the number in the order it was pushed by), zero-indexed. The +2
 *           is an added offset for the calling function's saved frame pointer and return pointer
 *           (pushed automatically by CALL, and popped by RET).
 *           so 32-bit assembly would like like so:
 *               mov ecx, [ebp + 8]  ; fie
 *               mov bl,  [ebp + 12] ; foe
 *               mov edx, [ebp + 16] ; high dword of fum
 *               mov eax, [ebp + 20] ; low dword of fum
 *           This will move fie into ECX, foe into BL, and fum into EAX and EDX
 *
 * [x86-64]
 *     1) The registers RDI, RSI, RDX, RCX, R8, and R9 are used for integer and memory address
 *        arguments. Only the 7-th arguments & onwards are passed using the stack
 *     2) Registers rbp, rbx and r12 through r15 “belong” to the calling function and the called
 *        function is required to preserve their values. In other words, a called function must
 *        preserve these registers’ values for its caller. Remaining registers “belong” to the 
 *        called function.
 *
 */
#if defined (__i386__)

#if 0
/*
 * do a context switch between the uthread pointed
 * register EAX & ECX are defined such that the caller responsibility is to save & restore them – hence we don’t need to save/restore them as part of the context.
 * we also ignore floating-point registers as we assume they are not used
 * TODO:
 * 1) what about SSE & friends regarding any compiler specific optimizations ?
 * 2) we need x86 & x86-64 versions
 * 3) consider the use of "volatile" to prevent optimizer from playing around here. would it make the use of "inline" safe ?
 *
 * BEWARE: inline assembly sometimes causes the compiler to generate wrong code. however, its better for performance bcz each function that calls for a context switch will have its own copy thereby allowing branch prediction to accurately predict the jumps. and it also save stack frame work :)
 *
 * Input:
 * first parameter is the destination stack, here its EAX register
 * first parameter is the source??? stack, here its EDX register
 */
/*static*/ void
silk_swap_stack_context(void***, void**) __attribute__((regparm(2)))
{
    __asm__ __volatile__ (
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
#endif // #if 0


void silk_create_initial_stack_context(struct silk_exec_state_t *initial_ctx,
                                       void    (*start_func) (void),
                                       char     *stack_buf,
                                       size_t    size)
{
    char *bottom = stack_buf + size;
    void** p = (void**)bottom;
    const intptr_t   btm = (uintptr_t)bottom;

    // verify 16-byte alignment as gcc ABI requires.
    assert((btm & ~0xf) == btm);

    // Stack frame alignment. TODO: is this a must ???
    p--; *p = 0;

    p--; *p = start_func; // EIP
    p--; *p = bottom;     // EBP
    p--; *p = 0;          // EBX
    p--; *p = 0;          // ESI
    p--; *p = 0;          // EDI

    // need every stack frame to be 16 byte aligned
    // TODO: is this a must ???
    p--; *p = 0;
    p--; *p = 0;

    // return to the entry point of the new silk thread.
    initial_ctx->esp = p;
}

__asm__ /*__volatile__*/ (
                      ".pushsection .text\n"
                      ".type silk_swap_stack_context, @function\n"
                      ".global silk_swap_stack_context\n"
                      "silk_swap_stack_context:\n"

                      // save frame pointer
                      "push %ebp\n"
                      "movl %esp, %ebp\n"

                      // save callee owned registers
                      "push %ebx\n"
                      "push %esi\n"
                      "push %edi\n"

                      // push 2 more DWORDs for 16-byte alignment
                      "pushl $0\n"
                      "pushl $0\n"

                      // switch stack, saving the the stack pointer in EAX (second argument)
                      "movl 0xc(%ebp), %edx\n"  // load address of old ESP variable into register
                      "movl %esp, (%edx)\n"     // save old ESP in the above address
                      "movl 0x8(%ebp), %esp\n"  // set ESP to the new area. the new ESP i read from old stack in relative addressing

                      // pop 2(?3?) extra DWORDs
                      "pop %edi\n"
                      "pop %edi\n"
                      //"pop %edi\n"

                      // restore callee owned registers
                      "pop %edi\n"
                      "pop %esi\n"
                      "pop %ebx\n"

                      // restore frame pointer
                      "pop  %ebp\n"

                      "ret\n"

                      ".popsection\n"
                      "");

#elif defined (__x86_64__)


void silk_create_initial_stack_context(struct silk_exec_state_t *initial_ctx,
                                       silk_uthread_func_t start_func,
                                       char     *stack_buf,
                                       size_t    size)
{
    char  *bottom = stack_buf + size;
    void  **p = (void**)bottom;
    const intptr_t   btm = (uintptr_t)bottom;

    // clear all registers for a clean start.
    memset(initial_ctx, 0, sizeof(*initial_ctx));
    // the return address in stack so we jump to it when we return 
    p--; *p = initial_func;
    initial_ctx->rsp = p;
}

/*
 * The general structure here (x86-64) is:
 *     1) function takes 2 arguments, which are passed in RDI (first) & RSI (second). The C prototype
 *        is “void silk_switch_ctx (strcut silk_context_t  *switch_from, strcut silk_context_t  *switch_to);”
 *     2) save registers of context A into a buffer pointed by register RDI
 *     3) switch stack pointer
 *     4) restore registers of context B from a buffer pointed by register RSI*
 * The registers we save are the stack pointer & all those that “belong” to caller !!! the rest of
 * the registers “belong” the the callee & hence the code which calls this function already saved them.
 *
 * BEWARE: 
 * we ignore floating-point, MMX & AVX registers here !!!
 */
void silk_swap_stack_context(struct silk_exec_state_t *switch_from, 
                             struct silk_exec_state_t *switch_to)
{
    asm(""
        ".pushsection .text\n"
        ".type silk_swap_stack_context, @function\n"
        ".global silk_swap_stack_context\n"
        "silk_swap_stack_context:\n"

        "moveq %rbx, 0(%rdi)\n"
        "moveq %rsp, 8(%rdi)\n"
        "moveq %rbp, 16(%rdi)\n"
        "moveq %r12, 24(%rdi)\n"
        "moveq %r13, 32(%rdi)\n"
        "moveq %r14, 40(%rdi)\n"
        "moveq %r15, 48(%rdi)\n"

        "xchg %rdi, %rsi\n"

        "movq 0(%rdi), %rbx\n"
        "movq 8(%rdi), %rsp\n"
        "movq 16(%rdi), %rbp\n"
        "movq 24(%rdi), %r12\n"
        "movq 32(%rdi), %r13\n"
        "movq 40(%rdi), %r14\n"
        "movq 48(%rdi), %r15\n"

        "retq\n"

        ".popsection\n"
        "");
}

#endif // __x86_64__

#endif // SILK_CONTEXT__MINIMAL

