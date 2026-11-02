// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer exec helpers
 *
 * Linux 7 owns kernel_execve() and the binfmt loading path.  This file only
 * retains the small architecture-specific register finalisation helper.
 *
 * Keep helpers SMALL — TCC has miscompiled the tail of large binfmt_flat
 * functions (workspace corruption).
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/binfmts.h>
#include <linux/printk.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>
#include <asm/t9000_softprot.h>

/* Phase 4: soft protection setup after bFLT load */
void t9k_on_exec_finish(struct pt_regs *regs);

/*
 * After transfer_args_to_stack + create_flat_tables built the stack at
 * phys addresses, bias every user pointer and SP into the Q3 LA window.
 * Called only when multi-quarter split is active (delta != 0).
 */
int transputer_flat_bias_stack(unsigned long delta)
{
	unsigned long __user *sp;
	long argc, i;

	if (!delta)
		return 0;

	sp = (unsigned long __user *)current->mm->start_stack;
	if (get_user(argc, sp))
		return -EFAULT;
	sp++;
	/* argvp + envp slots */
	{
		unsigned long v;
		if (get_user(v, sp) || put_user(v + delta, sp))
			return -EFAULT;
		sp++;
		if (get_user(v, sp) || put_user(v + delta, sp))
			return -EFAULT;
		sp++;
	}
	for (i = 0; i < argc; i++, sp++) {
		unsigned long v;
		if (get_user(v, sp) || put_user(v + delta, sp))
			return -EFAULT;
	}
	sp++; /* argv NULL */
	for (;;) {
		unsigned long v;
		if (get_user(v, sp))
			return -EFAULT;
		if (!v)
			break;
		if (put_user(v + delta, sp))
			return -EFAULT;
		sp++;
	}
	current->mm->start_stack += delta;
	current->mm->arg_start += delta;
	current->mm->arg_end += delta;
	current->mm->env_start += delta;
	current->mm->env_end += delta;
	return 0;
}

/*
 * transputer_flat_finish — final exec setup, in its own compilation unit.
 *
 * Factored out of load_flat_binary() because TCC miscompiles the tail of
 * that large function (layout-sensitive workspace corruption).  Putting this
 * in a separate .o gives TCC a fresh, small workspace frame.
 */
int transputer_flat_finish(struct pt_regs *regs,
			   unsigned long entry, unsigned long stack)
{
	current->mm->start_stack = stack;
	set_fs(USER_DS);
	regs->iptr  = entry;
	regs->wptr  = stack;
	regs->areg  = 0;
	regs->breg  = 0;
	regs->creg  = 0;
	regs->status = 0x1;
	/* Soft P-process regions for this task (uaccess / future goprot). */
	t9k_on_exec_finish(regs);
	return 0;
}
