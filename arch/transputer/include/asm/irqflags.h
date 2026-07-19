/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_IRQFLAGS_H
#define _ASM_TRANSPUTER_IRQFLAGS_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

/* Transputer interrupt flag handling
 * CRITICAL: Use extern function declarations, NOT static inline!
 * TCC has a bug where it generates recursive calls for inline functions
 * instead of inlining them, causing infinite recursion (2026-01-16)
 * Actual implementations are in arch/transputer/kernel/irqflags_stub.c
 */

extern unsigned long arch_local_save_flags(void);
/*
 * WORKAROUND (2026-01-17): TCC has a bug where do { } while(0) macros
 * use the last expression's return value instead of constant 0 for the
 * while condition. For void functions, AReg is garbage after return,
 * and if it happens to be 0, the loop repeats infinitely.
 *
 * Fix: Change void functions to return non-zero value so AReg != 0.
 */
extern int arch_local_irq_restore(unsigned long flags);
extern int arch_local_irq_disable(void);
extern int arch_local_irq_enable(void);
extern unsigned long arch_local_irq_save(void);
extern int arch_irqs_disabled_flags(unsigned long flags);
extern int arch_irqs_disabled(void);

#endif /* __ASSEMBLY__ */

/* Don't define raw_* macros - they are provided by linux/irqflags.h */

#endif /* _ASM_TRANSPUTER_IRQFLAGS_H */
