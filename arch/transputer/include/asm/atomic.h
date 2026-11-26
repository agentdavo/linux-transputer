/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_ATOMIC_H
#define _ASM_TRANSPUTER_ATOMIC_H

/*
 * The generic implementation uses arch_cmpxchg loops under CONFIG_SMP,
 * backed by the shared-board command engine for shared addresses. The UP
 * configuration retains local IRQ exclusion. See kernel/smp_locks.c.
 */
#include <asm-generic/atomic.h>
#include <asm-generic/atomic64.h>

#endif /* _ASM_TRANSPUTER_ATOMIC_H */
