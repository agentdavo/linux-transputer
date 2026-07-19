// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer Linux kernel main
 * Modular structure inspired by Minix demo
 */

/* Basic types */
typedef unsigned int u32;
typedef unsigned char u8;

#include <linux/init.h>
#include <asm/uart.h>

extern void start_kernel(void);
extern void setup_arch(char **cmdline_p);

/*
 * Keep the handoff diagnostic architecture-local.  The Transputer linker
 * prefers this wrapper when present, allowing us to distinguish a bad
 * bootstrap CALL from a failure inside the generic start_kernel path.
 */
void __init start_kernel_wrapper(void)
{
	uart_putc_raw('K');
	start_kernel();
	uart_putc_raw('R');
	for (;;)
		;
}

/* Kernel subsystem initialization - like Minix modules */
/* NOTE: Do NOT define functions named mm_init, sched_init, etc. here!
 * They shadow the real kernel functions and can cause the linker to
 * resolve cross-file calls to these dummies instead of the real ones. */

/* Minimal test entry point (kept for bring-up experiments only). */
void __init transputer_test_start_kernel(void)
{
    /* Dummy code to avoid J 0 */
    int dummy = 0;
    dummy++;

	/* Continue into the real kernel init. */
	setup_arch((char **)0);

    /* Halt if start_kernel returns. */
    while (1) {
    }
}
