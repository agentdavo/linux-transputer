/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_BITOPS_H
#define _ASM_TRANSPUTER_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/sched.h>

/* Atomic bitops use the generic UP atomic implementation in asm/atomic.h. */
#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/non-atomic.h>

#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>

#endif /* _ASM_TRANSPUTER_BITOPS_H */
