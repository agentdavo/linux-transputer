// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer exec helpers
 *
 * Linux 7 owns kernel_execve() and the binfmt loading path.  This file only
 * retains the small architecture-specific register finalisation helper.
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

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
	return 0;
}
