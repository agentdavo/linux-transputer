/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_PTRACE_H
#define _ASM_TRANSPUTER_PTRACE_H

#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__

/* Kernel pt_regs structure - same layout as user_regs_struct */
struct pt_regs {
	unsigned long wptr;	/* Workspace pointer */
	unsigned long iptr;	/* Instruction pointer */
	unsigned long areg;	/* A register */
	unsigned long breg;	/* B register */
	unsigned long creg;	/* C register */
	unsigned long status;	/* Status register */
};

#define user_mode(regs) ((regs)->status & 0x1)

#define instruction_pointer(regs) ((regs)->iptr)
#define user_stack_pointer(regs) ((regs)->wptr)

static inline void instruction_pointer_set(struct pt_regs *regs,
                                           unsigned long val)
{
	regs->iptr = val;
}

/* Get pointer to pt_regs for a task */
#define task_pt_regs(task) \
	((struct pt_regs *) (task_stack_page(task) + THREAD_SIZE) - 1)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TRANSPUTER_PTRACE_H */
