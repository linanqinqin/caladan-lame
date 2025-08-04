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
 * handling. Currently just returns with iretq for basic testing of LAME delivery.
 */
.align 16
.globl __lame_entry
.type __lame_entry, @function
__lame_entry:
	/* Empty function body - just return from LAME exception */
	iretq 