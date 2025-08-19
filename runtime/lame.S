/*
 * lame.S - LAME (Latency-Aware Memory Exception) handler entry point
 */

.file "lame.S"
.section        .note.GNU-stack,"",@progbits
.text

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