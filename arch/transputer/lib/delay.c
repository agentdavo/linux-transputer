// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer delay functions
 */

#include <linux/delay.h>
#include <linux/module.h>

void __delay(unsigned long loops)
{
	/* Simple delay loop for transputer
	 * Use explicit comparison instead of while(loops--) to avoid
	 * TCC miscompilation of the post-decrement loop pattern.
	 */
	while (loops > 0) {
		loops--;
		__asm__ __volatile__("" ::: "memory");
	}
}
EXPORT_SYMBOL(__delay);

void __udelay(unsigned long usecs)
{
	/* Convert microseconds to approximate loop count
	 * This is very basic - real implementation would use timer
	 */
	__delay(usecs * 10);
}
EXPORT_SYMBOL(__udelay);

void __ndelay(unsigned long nsecs)
{
	/* Convert nanoseconds to approximate loop count */
	__delay(nsecs / 100);
}
EXPORT_SYMBOL(__ndelay);
