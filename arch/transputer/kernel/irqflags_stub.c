// SPDX-License-Identifier: GPL-2.0
/*
 * IRQ flags stub implementations for Transputer
 *
 * TCC has a critical bug where it generates recursive inline function calls
 * instead of inlining them. This file provides explicit non-inline stubs
 * to prevent the recursion bug (2026-01-16).
 *
 * Soft IRQ mask: the T800 guest has no real host IRQ controller.  We still
 * track a software "irqs disabled" flag so early-boot code that checks
 * irqs_disabled() / local_irq_save() behaves correctly.  Without this, the
 * stubs always reported "enabled", which produces "Interrupts were enabled
 * *very* early" warnings and can let sleeping paths believe preemption is
 * legal during start_kernel.
 *
 * WORKAROUND (2026-01-17, updated 2026-07-21): TCC do { } while(0) macros
 * compare the last expression result to the while constant via EQC+CJ.
 * For while(0), the body must leave AReg == 0 so EQC 0 → 1 and CJ does
 * not re-enter.  All "void-like" helpers therefore return 0.
 */

#include <linux/types.h>

/* Do NOT include asm/irqflags.h - we're implementing the functions it declares */

/*
 * Non-zero means IRQs are considered disabled (matches arch_irqs_disabled_flags).
 * Boot with IRQs software-masked until the first local_irq_enable().
 */
static unsigned long tp_irq_disabled = 1;

unsigned long arch_local_save_flags(void)
{
	return tp_irq_disabled;
}

int arch_local_irq_restore(unsigned long flags)
{
	tp_irq_disabled = flags ? 1 : 0;
	return 0;
}

int arch_local_irq_disable(void)
{
	tp_irq_disabled = 1;
	return 0;
}

int arch_local_irq_enable(void)
{
	tp_irq_disabled = 0;
	return 0;
}

unsigned long arch_local_irq_save(void)
{
	unsigned long flags = tp_irq_disabled;
	tp_irq_disabled = 1;
	return flags;
}

int arch_irqs_disabled_flags(unsigned long flags)
{
	return flags != 0;
}

int arch_irqs_disabled(void)
{
	return tp_irq_disabled != 0;
}
