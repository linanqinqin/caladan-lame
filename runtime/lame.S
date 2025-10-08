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
 * __lame_entry - LAME handler entry point (via INT 0x1f)
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

	/* no need to check if preemption is already disabled, because INT 0x1f is never called in Caladan scheduler context */

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
 * __lame_entry2 - maximally optimized LAME handler (via INT 0x1f) for bundle size 2
 * 
 * This is a flattened, branch-free assembly implementation specifically
 * optimized for bundle size 2. It performs the following steps:
 * 1. Save volatile CPU state (except callee registers, rsp, rip, rflags)
 * 2. Get current kthread from perthread storage
 * 3. Check if LAME scheduling is enabled (branch-free) and if bundle.used == 2
 * 4. Get current uthread from bundle[active]
 * 5. Toggle active index using XOR (0 <-> 1)
 * 6. Get next uthread from bundle[active]
 * 7. Perform context switch using flattened __lame_jmp_thread_direct
 * 8. jmp to new uthread
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

	/* no need to check if preemption is already disabled, because INT 0x1f is never called in Caladan scheduler context */

	/* Disable preemption to protect LAME context switch atomicity */
	addl	$1, %gs:__perthread_preempt_cnt(%rip)

	/* Save volatile registers to stack */
	pushq	%rax
	pushq	%rcx
	pushq	%rdx

#ifdef CONFIG_LAME_TSC
	/* Save start TSC to rcx */
	pushq	%rbx
	xorq	%rax, %rax
	cpuid
	rdtsc
	shlq	$32, %rdx
	orq		%rdx, %rax
	movq	%rax, %rcx 	/* RCX = start tsc */
	popq	%rbx
#endif

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
	
	/* restore EAX back to unscaled active index, then toggle */
	shrl	$2, %eax		/* unscale back to 0/1 */
	xorl	$1, %eax		/* next = active ^ 1 */
	movl	%eax, LAME_BUNDLE_OFFSET + LAME_BUNDLE_ACTIVE(%r8)	/* Store new active index */

	/* Get next uthread pointer: bundle.uthreads[active].uthread */
	shll	$2, %eax		/* Multiply new index by 4 (since 32/8 = 4) */
	movq	(%r9, %rax, 8), %r11	/* Load next uthread pointer */

	/* Update __self to point to the new uthread */
	movq	%r11, %gs:__perthread___self(%rip)

	/* Flattened context switch - save current thread state */
	/* Save IP and stack pointer to current thread's trapframe */
	/* IP should point to .lame_entry2_exit for when we return to this thread */
	leaq	.lame_entry2_exit(%rip), %r9	/* Get address of exit label */
	movq	%r9, THREAD_TF_OFFSET + RIP(%r10)	/* Save RIP to current tf */
	movq	%rsp, THREAD_TF_OFFSET + RSP(%r10)	/* Save current RSP to current tf */

	/* Save callee-saved registers to current thread's trapframe */
	movq	%rbx, THREAD_TF_OFFSET + RBX(%r10)
	movq	%rbp, THREAD_TF_OFFSET + RBP(%r10)
	movq	%r12, THREAD_TF_OFFSET + R12(%r10)
	movq	%r13, THREAD_TF_OFFSET + R13(%r10)
	movq	%r14, THREAD_TF_OFFSET + R14(%r10)
	movq	%r15, THREAD_TF_OFFSET + R15(%r10)

	/* Restore next thread's state */
	/* Restore IP and stack pointer from next thread's trapframe */
	movq	THREAD_TF_OFFSET + RSP(%r11), %rsp	/* Restore stack pointer */
	movq	THREAD_TF_OFFSET + RIP(%r11), %r9	/* Get RIP from next tf */

	/* Restore callee-saved registers from next thread's trapframe */
	movq	THREAD_TF_OFFSET + RBX(%r11), %rbx
	movq	THREAD_TF_OFFSET + RBP(%r11), %rbp
	movq	THREAD_TF_OFFSET + R12(%r11), %r12
	movq	THREAD_TF_OFFSET + R13(%r11), %r13
	movq	THREAD_TF_OFFSET + R14(%r11), %r14
	movq	THREAD_TF_OFFSET + R15(%r11), %r15

	/* Set first argument (in case new thread) */
	movq	THREAD_TF_OFFSET + RDI(%r11), %rdi	/* ARG0 */

	/* Re-enable preemption after context switch is complete */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)

#ifdef CONFIG_LAME_TSC
	pushq	%rcx 	/* save rcx; rdtscp clobbers rcx */
	/* save end tsc, calculate elapsed tsc, and update bundle totals */
	rdtscp
	shlq	$32, %rdx
	orq		%rdx, %rax			/* RAX = end_tsc */
	popq	%rcx
	subq	%rcx, %rax			/* RAX = delta */
	addq	%rax, LAME_BUNDLE_OFFSET + LAME_BUNDLE_TOTAL_CYCLES(%r8)
	addq	$1, LAME_BUNDLE_OFFSET + LAME_BUNDLE_TOTAL_LAMES(%r8)
	pushq	%rbx
	xorq	%rax, %rax
	cpuid
	popq	%rbx
#endif

	/* Jump into next thread's trapframe */
	jmpq	*%r9

.lame_entry2_noswitch:
	/* Re-enable preemption after context switch is complete */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)

#ifdef CONFIG_LAME_TSC
	/* exit path for no switch; does not take part in TSC accounting */
	popq	%r11
	popq	%r10
	popq	%r9
	popq	%r8
	popq	%rdi
	popq	%rsi
	popq	%rdx
	popq	%rcx
	popq	%rax

	iretq
#endif

/* This point is reached when switching back to this thread */
.lame_entry2_exit: 

#ifdef CONFIG_LAME_TSC
	/* catching the TSC accounting for the return path */
	/* Save start TSC to rcx */
	pushq	%rbx
	xorq	%rax, %rax
	cpuid
	rdtsc
	shlq	$32, %rdx
	orq		%rdx, %rax
	movq	%rax, %rcx 	/* RCX = start tsc */
	popq	%rbx
#endif

	/* Restore volatile registers from stack */
	popq	%r11
	popq	%r10
	popq	%r9
	popq	%r8
	popq	%rdi
	popq	%rsi

#ifdef CONFIG_LAME_TSC
	pushq	%rcx
	/* save end tsc, calculate elapsed tsc, and update bundle totals */
	rdtscp
	shlq	$32, %rdx
	orq		%rdx, %rax			/* RAX = end_tsc */
	popq	%rcx
	subq	%rcx, %rax			/* RAX = delta */
	movq	%gs:__perthread_mykthread(%rip), %rdx
	addq	%rax, LAME_BUNDLE_OFFSET + LAME_BUNDLE_TOTAL_CYCLES(%rdx)
	/* do not increment total_lames; it's already incremented in the switch-to path */
	pushq	%rbx
	xorq	%rax, %rax
	cpuid
	popq	%rbx
#endif

	popq	%rdx
	popq	%rcx
	popq	%rax

	/* Return from LAME exception */
	iretq


/**
 * __lame_entry_nop - LAME handler entry point (via INT 0x1f) with nop
 * 
 * This handler simply does nothing and iretq
 * This is used for TSC measurement as the baseline
 */
.align 16
.globl __lame_entry_nop
.type __lame_entry_nop, @function
__lame_entry_nop:

	iretq

/**
 * __lame_entry2_pretend - LAME handler entry point (via INT 0x1f) for bundle size 2 with pretend context switch
 * 
 * This handler does the necessary context switch logic for bundle size 2, 
 * but does not actually switch to the next uthread.
 * This is used for TSC measurement as the baseline + context switch cost
 */
.align 16
.globl __lame_entry2_pretend
.type __lame_entry2_pretend, @function
__lame_entry2_pretend:

	/* no need to check if preemption is already disabled, because INT 0x1f is never called in Caladan scheduler context */

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
	jz	.lame_entry2_pretend_noswitch	/* Exit if disabled */

	/* Check if bundle has exactly 2 uthreads (used == 2) */
	cmpl	$2, LAME_BUNDLE_OFFSET + LAME_BUNDLE_USED(%r8)	/* bundle.used */
	jne	.lame_entry2_pretend_noswitch	/* Exit if not exactly 2 (this means there is only one uthread in the bundle, thus no switching needed) */

	/* Get current active index */
	movl	LAME_BUNDLE_OFFSET + LAME_BUNDLE_ACTIVE(%r8), %eax	/* bundle.active */

	/* Calculate current uthread pointer: bundle.uthreads[active].uthread */
	leaq	LAME_BUNDLE_OFFSET + LAME_BUNDLE_UTHREADS(%r8), %r9	/* bundle.uthreads base */
	shll	$2, %eax		/* Multiply index by 4 (since 32/8 = 4) */
	movq	(%r9, %rax, 8), %r10	/* Load current uthread pointer */
	
	/* restore EAX back to unscaled active index, then toggle */
	shrl	$2, %eax		/* unscale back to 0/1 */
	xorl	$1, %eax		/* next = active ^ 1 */
	movl	%eax, LAME_BUNDLE_OFFSET + LAME_BUNDLE_ACTIVE(%r8)	/* Store new active index */

	/* Get next uthread pointer: bundle.uthreads[active].uthread */
	shll	$2, %eax		/* Multiply new index by 4 (since 32/8 = 4) */
	movq	(%r9, %rax, 8), %r11	/* Load next uthread pointer */

	/* Update __self to point to the new uthread */
	movq	%r11, %gs:__perthread___self(%rip)

	/* Flattened context switch - save current thread state */
	/* Save IP and stack pointer to current thread's trapframe */
	/* IP should point to .lame_entry2_exit for when we return to this thread */
	leaq	.lame_entry2_pretend_exit(%rip), %r9	/* Get address of exit label */
	movq	%r9, THREAD_TF_OFFSET + RIP(%r10)	/* Save RIP to current tf */
	movq	%rsp, THREAD_TF_OFFSET + RSP(%r10)	/* Save current RSP to current tf */

	/* Save callee-saved registers to current thread's trapframe */
	movq	%rbx, THREAD_TF_OFFSET + RBX(%r10)
	movq	%rbp, THREAD_TF_OFFSET + RBP(%r10)
	movq	%r12, THREAD_TF_OFFSET + R12(%r10)
	movq	%r13, THREAD_TF_OFFSET + R13(%r10)
	movq	%r14, THREAD_TF_OFFSET + R14(%r10)
	movq	%r15, THREAD_TF_OFFSET + R15(%r10)

	/* Restore next thread's state */
	/* Restore IP and stack pointer from next thread's trapframe */
	movq	THREAD_TF_OFFSET + RSP(%r11), %rsp	/* Restore stack pointer */
	movq	THREAD_TF_OFFSET + RIP(%r11), %r9	/* Get RIP from next tf */

	/* Restore callee-saved registers from next thread's trapframe */
	movq	THREAD_TF_OFFSET + RBX(%r11), %rbx
	movq	THREAD_TF_OFFSET + RBP(%r11), %rbp
	movq	THREAD_TF_OFFSET + R12(%r11), %r12
	movq	THREAD_TF_OFFSET + R13(%r11), %r13
	movq	THREAD_TF_OFFSET + R14(%r11), %r14
	movq	THREAD_TF_OFFSET + R15(%r11), %r15

	/* Set first argument (in case new thread) */
	movq	THREAD_TF_OFFSET + RDI(%r11), %rdi	/* ARG0 */

	/* revert context switch back to the original uthread */
	/* note: this adds extra cycles to TSC measurement, but the memory loads should be L1D hit */
	movq	%r10, %gs:__perthread___self(%rip) /* reset __self to the original uthread */
	movl	$0, LAME_BUNDLE_OFFSET + LAME_BUNDLE_ACTIVE(%r8) /* reset active index */
	movq	THREAD_TF_OFFSET + RSP(%r10), %rsp	/* Restore stack pointer */
	movq	THREAD_TF_OFFSET + RIP(%r10), %r9	/* Get RIP from next tf */
	/* reset callee-saved registers to the original uthread */
	movq	THREAD_TF_OFFSET + RBX(%r10), %rbx
	movq	THREAD_TF_OFFSET + RBP(%r10), %rbp
	movq	THREAD_TF_OFFSET + R12(%r10), %r12
	movq	THREAD_TF_OFFSET + R13(%r10), %r13
	movq	THREAD_TF_OFFSET + R14(%r10), %r14
	movq	THREAD_TF_OFFSET + R15(%r10), %r15
	movq	THREAD_TF_OFFSET + RDI(%r10), %rdi	/* ARG0 */
	addq	$1, LAME_BUNDLE_OFFSET + LAME_BUNDLE_TOTAL_LAMES(%r8) /* increment total_lames */

	/* Re-enable preemption after context switch is complete */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)

	/* Jump into next thread's trapframe */
	jmpq	*%r9

.lame_entry2_pretend_noswitch:
	/* Re-enable preemption after context switch is complete */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)

/* This point is reached when switching back to this thread */
.lame_entry2_pretend_exit: 

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
 * __lame_entry_ret - LAME handler entry point (via PMI) that returns with ret
 * 
 * this version is problematic due to user stack corruption by direct push/pop.
 */
.align 16
.globl __lame_entry_ret
.type __lame_entry_ret, @function
__lame_entry_ret:

	/* check if preemption is already disabled. This usually indiates that either:
	 * 1. regular scheduling is happening
	 * 2. a LAME handler is already running 
	 */
	cmpb	$0, %gs:__perthread_preempt_cnt(%rip)
	je	1f
	/* if preemption is already disabled, just return */
	retq

1:
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

	/* set first argument to the top of the stack */
	// movq	%rsp, %rdi
	// addq	$8*9, %rdi

	/* realign stack */
	movq 	%rsp, %rax
	andq	$15, %rax
	cmpq 	$0, %rax
	je 	2f
	subq	$8, %rsp
	// call	lame_handle_ret
	call	lame_handle
	addq	$8, %rsp
	jmp	__lame_entry_ret_out

2:
	// call	lame_handle_ret
	call	lame_handle

__lame_entry_ret_out:
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
	retq


/**
 * __lame_entry2_ret - optimized LAME handler (via PMU) that returns with ret, for bundle size 2
 *
 * the handler does not touch the stack, since a PMI could arrive at arbitrary point in the code
 * with a stack pointer floating on the red zone. direct push/pop on the stack can corrupt the user program. 
 * instead, we use a gs scratch space and thread_t's trapframe.
 */
.align 16
.globl __lame_entry2_ret
.type __lame_entry2_ret, @function
__lame_entry2_ret:

	/* check if preemption is already disabled. This usually indiates that either:
	 * 1. regular scheduling is happening
	 * 2. a LAME handler is already running 
	 */
	cmpb	$0, %gs:__perthread_preempt_cnt(%rip)
	je	1f
	/* if preemption is already disabled, just return */
	retq

1:
	/* Disable preemption to protect LAME context switch atomicity */
	addl	$1, %gs:__perthread_preempt_cnt(%rip)

	/* save rax to LAME scratch space; load __self into rax */
	movq	%rax, %gs:__perthread_lame_scratch(%rip)
	movq	%gs:__perthread___self(%rip), %rax

	/* save volatile registers (except rax) to __self's trapframe; follow the order in thread_tf */
	movq 	%rdi, THREAD_TF_OFFSET + RDI(%rax)
	movq 	%rsi, THREAD_TF_OFFSET + RSI(%rax)
	movq 	%rdx, THREAD_TF_OFFSET + RDX(%rax)
	movq 	%rcx, THREAD_TF_OFFSET + RCX(%rax)
	movq 	%r8,  THREAD_TF_OFFSET + R8(%rax)
	movq 	%r9,  THREAD_TF_OFFSET + R9(%rax)
	movq 	%r10, THREAD_TF_OFFSET + R10(%rax)
	movq 	%r11, THREAD_TF_OFFSET + R11(%rax)
	
	/* restore rax from scratch and save it to __self's trapframe */
	movq 	%rax, %r8
	movq 	%gs:__perthread_lame_scratch(%rip), %rax
	movq 	%rax, THREAD_TF_OFFSET + RAX(%r8)

	/* load mykthread into r8 */
	movq	%gs:__perthread_mykthread(%rip), %r8

	/* Check if LAME scheduling is enabled (branch-free) */
	movb	LAME_BUNDLE_OFFSET + LAME_BUNDLE_ENABLED(%r8), %al	/* bundle.enabled */
	testb	%al, %al
	jz	.lame_entry2_ret_noswitch	/* Exit if disabled */

	/* Check if bundle has exactly 2 uthreads (used == 2) */
	cmpl	$2, LAME_BUNDLE_OFFSET + LAME_BUNDLE_USED(%r8)	/* bundle.used */
	jne	.lame_entry2_ret_noswitch	/* Exit if not exactly 2 (this means there is only one uthread in the bundle, thus no switching needed) */

	/* Get current active index */
	movl	LAME_BUNDLE_OFFSET + LAME_BUNDLE_ACTIVE(%r8), %eax	/* bundle.active */

	/* Calculate current uthread pointer: bundle.uthreads[active].uthread */
	leaq	LAME_BUNDLE_OFFSET + LAME_BUNDLE_UTHREADS(%r8), %r9	/* bundle.uthreads base */
	shll	$2, %eax		/* Multiply index by 4 (since 32/8 = 4) */
	movq	(%r9, %rax, 8), %r10	/* Load current uthread pointer */
	
	/* restore EAX back to unscaled active index, then toggle */
	shrl	$2, %eax		/* unscale back to 0/1 */
	xorl	$1, %eax		/* next = active ^ 1 */
	movl	%eax, LAME_BUNDLE_OFFSET + LAME_BUNDLE_ACTIVE(%r8)	/* Store new active index */

	/* Get next uthread pointer: bundle.uthreads[active].uthread */
	shll	$2, %eax		/* Multiply new index by 4 (since 32/8 = 4) */
	movq	(%r9, %rax, 8), %r11	/* Load next uthread pointer */

	/* Update __self to point to the new uthread */
	movq	%r11, %gs:__perthread___self(%rip)

	/* Flattened context switch - save current thread state */
	/* Save IP and stack pointer to current thread's trapframe */
	/* IP should point to .lame_entry2_exit for when we return to this thread */
	leaq	.lame_entry2_ret_exit(%rip), %r9	/* Get address of exit label */
	movq	%r9, THREAD_TF_OFFSET + RIP(%r10)	/* Save RIP to current tf */
	movq	%rsp, THREAD_TF_OFFSET + RSP(%r10)	/* Save current RSP to current tf */

	/* Save callee-saved registers to current thread's trapframe */
	movq	%rbx, THREAD_TF_OFFSET + RBX(%r10)
	movq	%rbp, THREAD_TF_OFFSET + RBP(%r10)
	movq	%r12, THREAD_TF_OFFSET + R12(%r10)
	movq	%r13, THREAD_TF_OFFSET + R13(%r10)
	movq	%r14, THREAD_TF_OFFSET + R14(%r10)
	movq	%r15, THREAD_TF_OFFSET + R15(%r10)

	/* Restore next thread's state */
	/* Restore IP and stack pointer from next thread's trapframe */
	movq	THREAD_TF_OFFSET + RSP(%r11), %rsp	/* Restore stack pointer */
	movq	THREAD_TF_OFFSET + RIP(%r11), %r9	/* Get RIP from next tf */

	/* Restore callee-saved registers from next thread's trapframe */
	movq	THREAD_TF_OFFSET + RBX(%r11), %rbx
	movq	THREAD_TF_OFFSET + RBP(%r11), %rbp
	movq	THREAD_TF_OFFSET + R12(%r11), %r12
	movq	THREAD_TF_OFFSET + R13(%r11), %r13
	movq	THREAD_TF_OFFSET + R14(%r11), %r14
	movq	THREAD_TF_OFFSET + R15(%r11), %r15

	/* Set first argument (in case new thread) */
	movq	THREAD_TF_OFFSET + RDI(%r11), %rdi	/* ARG0 */

	/* Re-enable preemption after context switch is complete */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)

	/* Jump into next thread's trapframe */
	jmpq	*%r9

.lame_entry2_ret_noswitch:
	/* Re-enable preemption after context switch is complete */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)

/* This point is reached when switching back to this thread */
.lame_entry2_ret_exit: 

	/* restore volatile regs from __self's trapframe; follow the order in thread_tf */
	movq	%gs:__perthread___self(%rip), %rax
	movq 	THREAD_TF_OFFSET + RDI(%rax), %rdi
	movq 	THREAD_TF_OFFSET + RSI(%rax), %rsi
	movq 	THREAD_TF_OFFSET + RDX(%rax), %rdx
	movq 	THREAD_TF_OFFSET + RCX(%rax), %rcx
	movq 	THREAD_TF_OFFSET + R8(%rax), %r8
	movq 	THREAD_TF_OFFSET + R9(%rax), %r9
	movq 	THREAD_TF_OFFSET + R10(%rax), %r10
	movq 	THREAD_TF_OFFSET + R11(%rax), %r11
	movq 	THREAD_TF_OFFSET + RAX(%rax), %rax

	/* Return from LAME exception */
	retq


/**
 * __lame_entry_stall_ret - LAME handler entry point (via PMI) that emulates a CPU stall
 * 
 * This is used for the CXL baseline (longer memory loads, but no LAME switching)
 */
.align 16
.globl __lame_entry_stall_ret
.type __lame_entry_stall_ret, @function
__lame_entry_stall_ret:
	
	/* check if preemption is already disabled. This usually indiates that either:
	 * 1. regular scheduling is happening
	 * 2. a LAME handler is already running 
	 */
	cmpb	$0, %gs:__perthread_preempt_cnt(%rip)
	je	1f
	/* if preemption is already disabled, just return */
	retq

1:
	
	/* Disable preemption */
	addl	$1, %gs:__perthread_preempt_cnt(%rip)

	/* save r8 to LAME scratch space; load mykthread into r8 */
	movq	%r8, %gs:__perthread_lame_scratch(%rip)
	movq	%gs:__perthread_mykthread(%rip), %r8

	/* Save volatile registers to LAME trapframe */
	movq 	%rax, LAME_BUNDLE_OFFSET + LAME_BUNDLE_TF + RAX(%r8)
	movq 	%rcx, LAME_BUNDLE_OFFSET + LAME_BUNDLE_TF + RCX(%r8)
	movq 	%rdx, LAME_BUNDLE_OFFSET + LAME_BUNDLE_TF + RDX(%r8)

	rdtsc
	shlq	$32, %rdx
	orq		%rdx, %rax /* rax = current tsc */
	addq 	$800, %rax /* rax = deadline tsc */

	/* Per WAITPKG/TPAUSE calling convention:
     *  ECX = hint (0 = C0.1)
     *  EDX:EAX = TSC deadline (low in EAX, high in EDX)
	 */
	movq    %rax, %rdx          /* EAX = low 32 */
	shrq    $32, %rdx           /* EDX = high 32 */
	xorl    %ecx, %ecx          /* EAX = hint (0 = C0.1) */

	tpause 	%ecx

	/* Restore volatile registers from stack */
	movq 	LAME_BUNDLE_OFFSET + LAME_BUNDLE_TF + RAX(%r8), %rax
	movq 	LAME_BUNDLE_OFFSET + LAME_BUNDLE_TF + RCX(%r8), %rcx
	movq 	LAME_BUNDLE_OFFSET + LAME_BUNDLE_TF + RDX(%r8), %rdx

	/* restore r8 from LAME scratch space */
	movq 	%gs:__perthread_lame_scratch(%rip), %r8

	/* Re-enable preemption */
	subl	$1, %gs:__perthread_preempt_cnt(%rip)
	retq

/**
 * __lame_entry_nop_ret - LAME handler entry point (via PMI) with nop
 * 
 * This handler simply does nothing and retq
 * This is used for as the baseline without CXL emulation
 */
.align 16
.globl __lame_entry_nop_ret
.type __lame_entry_nop_ret, @function
__lame_entry_nop_ret:

	retq

