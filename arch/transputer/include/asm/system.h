/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SYSTEM_H
#define _ASM_TRANSPUTER_SYSTEM_H

/*
 * asm/system.h for transputer architecture
 *
 * In Linux 2.6.39, this is a monolithic header. Later kernels split it
 * into barrier.h, cmpxchg.h, switch_to.h, etc. Since the transputer
 * port has those as separate files, we just include them here.
 */

#include <asm/barrier.h>
#include <asm/cmpxchg.h>

#ifndef __ASSEMBLY__

#include <asm/irqflags.h>

/* nop instruction */
#define nop()  asm volatile("" ::: "memory")

/*
 * switch_to() - context switch macro
 * On a single-core no-MMU transputer this is minimal.
 */
struct task_struct;
struct thread_info;

extern struct task_struct *__switch_to(struct task_struct *prev,
                                       struct task_struct *next);

#define switch_to(prev, next, last)                     \
    do {                                                \
        (last) = __switch_to((prev), (next));           \
    } while (0)

/*
 * arch_align_stack - align the stack pointer
 */
static inline unsigned long arch_align_stack(unsigned long sp)
{
    return sp & ~0x3;  /* 4-byte alignment for 32-bit transputer */
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TRANSPUTER_SYSTEM_H */
