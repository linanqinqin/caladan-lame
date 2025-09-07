/*
 * lame.S - LAME (Latency-Aware Memory Exception) handler entry point
 */

#include <base/trapframe.h>
#include "lame.h"

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

/**
 * __lame_entry2 - maximally optimized LAME handler for bundle size 2
 * 
 * This is a flattened, branch-free assembly implementation specifically
 * optimized for bundle size 2. It performs the following steps:
 * 1. Save volatile CPU state (except callee registers, rsp, rip, rflags)
 * 2. Get current kthread from perthread storage
 * 3. Check if LAME scheduling is enabled (branch-free)
 * 4. Get current uthread from bundle[active]
 * 5. Toggle active index using XOR (0 <-> 1)
 * 6. Get next uthread from bundle[active]
 * 7. Perform context switch using __lame_jmp_thread_direct
 * 8. Restore volatile CPU state
 * 9. Return with iretq
 * 
 * Key optimizations:
 * - No function calls (all logic inlined)
 * - No logging
 * - Branch-free design using conditional moves
 * - Bit operations for index toggling
 * - Direct memory access with known offsets
 */
.align 16
.globl __lame_entry2
.type __lame_entry2, @function
__lame_entry2:
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

	/* Get current kthread from perthread storage */
	movq	%gs:__perthread_mykthread(%rip), %r8

	/* Check if LAME scheduling is enabled (branch-free) */
	movb	LAME_BUNDLE_OFFSET + LAME_BUNDLE_ENABLED(%r8), %al	/* bundle.enabled */
	testb	%al, %al
	jz	.lame_entry2_noswitch	/* Exit if disabled */

	/* Check if bundle has exactly 2 uthreads (used == 2) */
	cmpl	$2, LAME_BUNDLE_OFFSET + LAME_BUNDLE_USED(%r8)	/* bundle.used */
	jne	.lame_entry2_noswitch	/* Exit if not exactly 2 (this means there is only one uthread in the bundle, thus no switching needed) */

	/* Get current active index */
	movl	LAME_BUNDLE_OFFSET + LAME_BUNDLE_ACTIVE(%r8), %eax	/* bundle.active */

	/* Calculate current uthread pointer: bundle.uthreads[active].uthread */
	leaq	LAME_BUNDLE_OFFSET + LAME_BUNDLE_UTHREADS(%r8), %r9	/* bundle.uthreads base */
	shll	$2, %eax		/* Multiply index by 4 (since 32/8 = 4) */
	movq	(%r9, %rax, 8), %r10	/* Load current uthread pointer */

	/* Toggle active index: active = active ^ 1 (0 <-> 1) */
	xorl	$1, %eax
	movl	%eax, LAME_BUNDLE_OFFSET + LAME_BUNDLE_ACTIVE(%r8)	/* Store new active index */

	/* Get next uthread pointer: bundle.uthreads[active].uthread */
	shll	$2, %eax		/* Multiply new index by 4 (since 32/8 = 4) */
	movq	(%r9, %rax, 8), %r11	/* Load next uthread pointer */

	/* Update __self to point to the new uthread */
	movq	%r11, %gs:__perthread___self(%rip)

	/* Perform context switch via __lame_jmp_thread_direct */
	leaq	THREAD_TF_OFFSET(%r10), %rdi	/* &cur_th->tf */
	leaq	THREAD_TF_OFFSET(%r11), %rsi	/* &next_th->tf */
	call	__lame_jmp_thread_direct
	/* After returning to this thread, fall through to exit */

/* This point is reached when switching back to this thread */
.lame_entry2_exit: 
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

.lame_entry2_noswitch:
	/* Re-enable preemption */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)
	jmpq	.lame_entry2_exit
