/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_CMPXCHG_H
#define _ASM_TRANSPUTER_CMPXCHG_H
#ifdef CONFIG_SMP
unsigned long tp_smp_cmpxchg(volatile void *, unsigned long, unsigned long, unsigned int);
unsigned long tp_smp_xchg(volatile void *, unsigned long, unsigned int);
#define arch_cmpxchg(p,o,n) ((__typeof__(*(p)))tp_smp_cmpxchg((p),(unsigned long)(o),(unsigned long)(n),sizeof(*(p))))
#define arch_xchg(p,n) ((__typeof__(*(p)))tp_smp_xchg((p),(unsigned long)(n),sizeof(*(p))))
#define arch_cmpxchg_local arch_cmpxchg
#else
#include <asm-generic/cmpxchg.h>
#endif
#endif
