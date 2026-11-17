/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_ATOMIC_H
#define _ASM_TRANSPUTER_ATOMIC_H

/*
 * The Transputer port is uniprocessor.  Linux's current generic atomic
 * implementation provides the required arch_atomic_* API by excluding local
 * interrupts around the read/modify/write sequence.
 */
#include <asm-generic/atomic.h>
#include <asm-generic/atomic64.h>

#endif /* _ASM_TRANSPUTER_ATOMIC_H */
