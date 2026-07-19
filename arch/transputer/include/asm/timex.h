/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer timer and cycle counter definitions
 *
 * The Transputer has a simple timer accessed via LDTIMER instruction.
 * It counts down at the CPU clock rate (typically 20MHz for T800).
 */

#ifndef _ASM_TRANSPUTER_TIMEX_H
#define _ASM_TRANSPUTER_TIMEX_H

#ifdef __KERNEL__

/*
 * Clock tick rate for legacy timing code
 * This isn't directly used on Transputer but kernel expects it defined
 */
#define CLOCK_TICK_RATE		1193182 /* Standard PC PIT frequency */

/*
 * Cycle counter type
 * Transputer timer is 32-bit counting down from 0xFFFFFFFF
 */
typedef unsigned int cycles_t;

/* External assembly helper for LDTIMER instruction */
extern unsigned int transputer_ldtimer(void);

/*
 * get_cycles - Read the Transputer cycle counter
 *
 * Uses the Transputer LDTIMER instruction to read the hardware timer.
 * The timer is cyclic and wraps from MostPos to MostNeg.
 *
 * For high priority processes: increments every 1μs
 * For low priority processes: increments every 64μs
 *
 * Note: Use sum/diff (modulo arithmetic) for time calculations,
 * not add/sub which would overflow at wraparound.
 */
static inline cycles_t get_cycles(void)
{
	/* Use assembly helper that implements:
	 * pfix 2, opr 2 = ldtimer (opcode 0x22)
	 * This pushes the current timer value into Areg
	 */
	return transputer_ldtimer();
}

#define get_cycles get_cycles

/*
 * random_get_entropy - Get entropy for random number generation
 *
 * Uses cycle counter as entropy source.
 * Falls back to generic implementation if no cycle counter.
 */
static inline unsigned long random_get_entropy(void)
{
	cycles_t cycles = get_cycles();

	if (cycles)
		return cycles;

	/* No hardware timer available, use generic fallback */
	return random_get_entropy_fallback();
}

#define random_get_entropy random_get_entropy

#endif /* __KERNEL__ */

#endif /* _ASM_TRANSPUTER_TIMEX_H */
