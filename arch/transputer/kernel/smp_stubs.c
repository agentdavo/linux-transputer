// SPDX-License-Identifier: GPL-2.0
/*
 * SMP/CPU stubs for Transputer (single-core, no SMP)
 * Architecture-specific CPU management for single-core system.
 */

#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/compiler.h>

/* CPU masks for single-core system */
struct cpumask __cpu_possible_mask __weak = { CPU_BITS_NONE };
struct cpumask __cpu_online_mask __weak = { CPU_BITS_NONE };
struct cpumask __cpu_present_mask __weak = { CPU_BITS_NONE };
struct cpumask __cpu_active_mask __weak = { CPU_BITS_NONE };
struct cpumask __cpu_enabled_mask __weak = { CPU_BITS_NONE };

EXPORT_SYMBOL(__cpu_possible_mask);
EXPORT_SYMBOL(__cpu_online_mask);
EXPORT_SYMBOL(__cpu_present_mask);
EXPORT_SYMBOL(__cpu_active_mask);

/* CPU bitmap for single-core - just CPU 0 */
const unsigned long cpu_all_bits[BITS_TO_LONGS(NR_CPUS)] __weak = {
	[0] = 1UL  /* Only CPU 0 is present */
};
EXPORT_SYMBOL(cpu_all_bits);

/* CPU bit bitmap - used for efficient bit operations */
const unsigned long cpu_bit_bitmap[BITS_PER_LONG+1][BITS_TO_LONGS(NR_CPUS)] __weak = {
	{},  /* Padding for off-by-one indexing */
	{ 1UL },  /* Bit 0 set */
};
EXPORT_SYMBOL(cpu_bit_bitmap);

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
