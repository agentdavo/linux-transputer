// SPDX-License-Identifier: GPL-2.0
/*
 * Early L-process THDS install (ISM §10 / PROCESS.md).
 *
 * Freestanding head runs as G (ThReg = NotProcess.p). Installing a real THDS
 * via selth makes IntegerOverflow / IllegalInstruction L-traps instead of
 * G-halting the CPU — required for robust Transputer/Linux bring-up.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/thds.h>
#include <asm/uart.h>

#ifdef CONFIG_TRANSPUTER_L_THDS_BOOT

/* Static THDS — 12 words, 64-byte aligned for clean workspace use as Wptr */
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
#include <linux/threads.h>
/* Pad each descriptor to a cache line, not just the start of the array. */
static unsigned long tp_cpu_thds[NR_CPUS][16] __aligned(64);
#define tp_boot_thds tp_cpu_thds[*(volatile unsigned int *)0x42000004UL]
#else
static unsigned long tp_boot_thds[THDS_WORDS] __aligned(64);
#endif

int tp_thds_boot_install(void)
{
	unsigned long th;
	unsigned long *t = tp_boot_thds;
	int i;

	uart_putc_raw('t'); /* start THDS install */

	for (i = 0; i < THDS_WORDS; i++)
		t[i] = 0;

	/* th.Cntl = default CW without ThInUse */
	t[THDS_CNTL] = TP32X_STATUS_DEFAULT_CW;
	/* th.Iptr = L-trap stub */
	t[THDS_IPTR] = (unsigned long)tp_ltrap_entry;
	t[THDS_FPTR] = TP32X_NOT_PROCESS_P;
	t[THDS_BPTR] = TP32X_NOT_PROCESS_P;

	tp_asm_selth((unsigned long)t);
	uart_putc_raw('s'); /* selth returned */
	th = tp_asm_ldth();
	uart_putc_raw('h'); /* ldth returned */

	/*
	 * Do not call pr_err/printk here — console is not safe yet and has
	 * sent the CPU to low Iptr on failure paths.  Raw UART only.
	 */
	if (th != (unsigned long)t) {
		uart_putc_raw('!');
		/* full hex of ldth result — was one nibble (e.g. 'c' from garbage
		 * when CALL frame only saved the link and ldl 1 read trash). */
		{
			unsigned int v = (unsigned int)th;
			int i;
			for (i = 28; i >= 0; i -= 4) {
				unsigned int n = (v >> i) & 0xF;
				uart_putc_raw(n < 10 ? '0' + n : 'a' + n - 10);
			}
		}
		/* Continue boot as G rather than dying */
		return -1;
	}

	uart_putc_raw('L'); /* L-process THDS live */
	/* Force clean 0 in AReg after UART store (setup checks return). */
	{
		volatile int z = 0;
		return z;
	}
}


#endif /* CONFIG_TRANSPUTER_L_THDS_BOOT */
