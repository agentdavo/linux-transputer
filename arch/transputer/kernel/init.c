// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer kernel-internal init (PID 1 bypass)
 *
 * Since kernel_execve() and binfmt_flat don't exist yet for transputer,
 * this function serves as the kernel-internal PID 1.  It halts with STOPP.
 */

void transputer_kernel_init(void)
{
	/* STOPP — deschedule this process (halt on uniprocessor) */
	__asm__(".byte 0x21, 0xf5");

	/* Should never reach here */
	while (1)
		;
}
