// SPDX-License-Identifier: GPL-2.0
/*
 * SMP/CPU stubs for Transputer (single-core, no SMP)
 *
 * CPU masks / cpu_bit_bitmap / cpu_all_bits live in kernel/cpu.c — do NOT
 * re-export weak copies here.  TCC's archive/object override is fragile; a
 * weak empty mask winning over cpu.o's real symbols leaves every online bit
 * clear and breaks for_each_online_cpu after boot_cpu_init.
 */

#include <linux/module.h>
#include <linux/compiler.h>

/* Loop delay calibration */
unsigned long loops_per_jiffy __weak = (1 << 12);
EXPORT_SYMBOL(loops_per_jiffy);

/* Delay function stub */
void __weak __delay(unsigned long cycles)
{
	unsigned long i;
	for (i = 0; i < cycles; i++)
		__asm__ __volatile__("" ::: "memory");
}
EXPORT_SYMBOL(__delay);

/* Conditional resched stub */
int __weak __cond_resched(void)
{
	/* Single-core, no preemption - just return */
	return 0;
}
EXPORT_SYMBOL(__cond_resched);
