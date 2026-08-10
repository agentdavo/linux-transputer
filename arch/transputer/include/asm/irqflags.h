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
 * WORKAROUND (2026-01-17, updated 2026-08): TCC do { } while(0) uses the
 * body's last expression as the while condition.  arch_* disable/enable/
 * restore therefore return 0.  raw_local_irq_save/save_flags still end with
 * `flags = arch_local_irq_save()` (often 1) — those macros force a trailing
 * 0 under CONFIG_TRANSPUTER in include/linux/irqflags.h.  Without that,
 * boot_cpu_init's first atomic bitop hangs after marker 'i'.
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
