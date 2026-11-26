/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_FUTEX_H
#define _ASM_TRANSPUTER_FUTEX_H
#ifdef CONFIG_SMP
#include <linux/types.h>
int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 oldval, u32 newval);
int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr);
#else
#include <asm-generic/futex.h>
#endif
#endif
