/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer interrupt definitions
 *
 * The Transputer doesn't have a traditional interrupt controller.
 * It uses hardware process scheduling and event channels instead.
 * For Linux, we simulate a minimal interrupt system for essential devices.
 */

#ifndef _ASM_TRANSPUTER_IRQ_H
#define _ASM_TRANSPUTER_IRQ_H

/*
 * Number of interrupt sources
 * Transputer minimal interrupt system:
 *   IRQ 0: Timer (using Transputer LDTIMER/LDINF)
 *   IRQ 1: UART/Console
 *   IRQ 2-15: Reserved for future devices
 */
#define NR_IRQS		16

/* Include generic IRQ infrastructure */
#include <asm-generic/irq.h>

/*
 * Transputer-specific IRQ numbers
 * Define symbolic names for common IRQs
 */
#define TRANSPUTER_IRQ_TIMER	0
#define TRANSPUTER_IRQ_UART	1

#endif /* _ASM_TRANSPUTER_IRQ_H */
