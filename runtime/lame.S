/*
 * lame.S - LAME (Latency-Aware Memory Exception) handler entry point
 */

#include <base/trapframe.h>

.file "lame.S"
.section        .note.GNU-stack,"",@progbits
.text

/**
 * __lame_jmp_thread_direct - LAME-specific context switch without preemption
 * @oldtf: the trap frame to save (%rdi)
 * @newtf: the trap frame to restore (%rsi)
 *
 * This function performs the same context switch as __jmp_thread_direct
 * but without the preemption handling and thread_running flag management,
 * preserving LAME's clean abstraction from Caladan's scheduling logic.
 * All uthreads in a bundle are considered "running" from Caladan's perspective.
 */
.align 16
.globl __lame_jmp_thread_direct
.type __lame_jmp_thread_direct, @function
__lame_jmp_thread_direct:
	/* save ip and stack */
	movq    (%rsp), %r8
	movq    %r8, RIP(%rdi)
	leaq    8(%rsp), %r8
	movq    %r8, RSP(%rdi)

	/* save callee regs */
	movq    %rbx, RBX(%rdi)
	movq    %rbp, RBP(%rdi)
	movq    %r12, R12(%rdi)
	movq    %r13, R13(%rdi)
	movq    %r14, R14(%rdi)
	movq    %r15, R15(%rdi)

	/* restore ip and stack */
	movq    RSP(%rsi), %rsp
	movq    RIP(%rsi), %r8

	/* restore callee regs */
	movq    RBX(%rsi), %rbx
	movq    RBP(%rsi), %rbp
	movq    R12(%rsi), %r12
	movq    R13(%rsi), %r13
	movq    R14(%rsi), %r14
	movq    R15(%rsi), %r15

	/* set first argument (in case new thread) */
	movq    RDI(%rsi), %rdi /* ARG0 */

	/* re-enable preemption after context switch is complete */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)

	/* jump into trap frame */
	jmpq	*%r8

/**
 * __lame_entry - LAME handler entry point
 * 
 * This is the assembly entry point for LAME (Latency-Aware Memory Exception)
 * handling. It performs the following steps:
 * 1. Save volatile CPU state (except callee registers, rsp, rip, rflags)
 * 2. Call lame_handle() to perform context switch logic
 * 3. Restore volatile CPU state
 * 4. Return with iretq
 */
.align 16
.globl __lame_entry
.type __lame_entry, @function
__lame_entry:
	/* linanqinqin */
	/* Disable preemption to protect LAME context switch atomicity */
	addl	$1, %gs:__perthread_preempt_cnt(%rip)

	/* Save volatile registers to stack */
	pushq	%rax
	pushq	%rcx
	pushq	%rdx
	pushq	%rsi
	pushq	%rdi
	pushq	%r8
	pushq	%r9
	pushq	%r10
	pushq	%r11

	/* Call C wrapper function to handle LAME logic */
	call	lame_handle

	/* Restore volatile registers from stack */
	popq	%r11
	popq	%r10
	popq	%r9
	popq	%r8
	popq	%rdi
	popq	%rsi
	popq	%rdx
	popq	%rcx
	popq	%rax

	/* Return from LAME exception */
	iretq
	/* end */ 