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
 * The int returns date from a TinyCC bug (2026-01-17) where
 * do { } while (0) used the body's last expression as the loop condition;
 * returning 0 kept the generic irqflags macros from looping.  TinyCC no
 * longer does that (checked 2026-11-02, and the generic macros and
 * boot_cpu_init's atomic bitops run unpatched), so these can become void.
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
