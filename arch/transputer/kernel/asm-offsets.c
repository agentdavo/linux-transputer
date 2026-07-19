// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer asm-offsets.c
 *
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#define COMPILE_OFFSETS
#include <linux/kbuild.h>
#include <linux/sched.h>
#include <linux/thread_info.h>
#include <linux/ptrace.h>
#include <linux/mm.h>
#include <asm/processor.h>

int main(void)
{
	/* offsets into the task_struct */
	DEFINE(TASK_FLAGS, offsetof(struct task_struct, flags));
	DEFINE(TASK_THREAD, offsetof(struct task_struct, thread));
	DEFINE(TASK_MM, offsetof(struct task_struct, mm));
	DEFINE(TASK_ACTIVE_MM, offsetof(struct task_struct, active_mm));

	/* offsets into thread_info */
	DEFINE(TI_TASK, offsetof(struct thread_info, task));
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_PREEMPT_COUNT, offsetof(struct thread_info, preempt_count));

	/* offsets into pt_regs */
	DEFINE(PT_WPTR, offsetof(struct pt_regs, wptr));
	DEFINE(PT_IPTR, offsetof(struct pt_regs, iptr));
	DEFINE(PT_AREG, offsetof(struct pt_regs, areg));
	DEFINE(PT_BREG, offsetof(struct pt_regs, breg));
	DEFINE(PT_CREG, offsetof(struct pt_regs, creg));
	DEFINE(PT_STATUS, offsetof(struct pt_regs, status));

	/* Other constants */
	DEFINE(PAGE_SIZE, PAGE_SIZE);
	DEFINE(THREAD_SIZE, THREAD_SIZE);

	return 0;
}
