// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer process management
 *
 * Based on arch/microblaze/kernel/process.c and arch/h8300/kernel/process.c.
 * Implements cooperative kernel threading for NOMMU single-CPU transputer.
 *
 * Context switching uses GAJW (exchange workspace pointer) — the transputer's
 * hardware context switch primitive. Since TCC cannot emit inline asm with
 * transputer instructions in C, __switch_to() is implemented in switch.S
 * using .byte sequences.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/unistd.h>
#include <linux/reboot.h>
#include <linux/init_task.h>

#include <asm/setup.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/segment.h>
#include <asm/thread_info.h>

/*
 * arch_cpu_idle — called from the idle loop.
 * On transputer, there's nothing to do (no WFI instruction).
 */
void arch_cpu_idle(void)
{
	/* barrier() could go here but is not needed for UP */
}

/*
 * cpu_idle — the idle loop, called from rest_init() after schedule().
 *
 * The standard pattern: check if rescheduling is needed, call schedule().
 * With real threading, the scheduler will dispatch kernel_init (PID 1)
 * and kthreadd (PID 2) when they are runnable.
 */
void cpu_idle(void)
{
	while (1) {
		while (!need_resched())
			arch_cpu_idle();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

/*
 * copy_thread — set up a new task's kernel stack for context switch.
 *
 * Places a pt_regs frame at the top of the new task's kernel stack,
 * and sets the saved context so that __switch_to() will "return" to
 * ret_from_fork, which then calls the thread function.
 */
extern void ret_from_fork(void);
extern void machine_halt(void);

#define TP_THREAD_STATUS_NEW	0x1

int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	struct pt_regs *childregs;
	struct pt_regs *regs = current_pt_regs();
	unsigned long *switch_frame;
	unsigned long kthread_fn = 0;
	unsigned long kthread_arg = 0;

	/* Place pt_regs at top of new kernel stack */
	childregs = (struct pt_regs *)
		((unsigned long)task_stack_page(p) + THREAD_SIZE) - 1;

	if (unlikely(args->fn)) {
		memset(childregs, 0, sizeof(*childregs));
		kthread_fn = (unsigned long)args->fn;
		kthread_arg = (unsigned long)args->fn_arg;
		childregs->status = 0;
	} else {
		*childregs = *regs;
		childregs->areg = 0;
		if (args->stack)
			childregs->wptr = args->stack;
	}

	/*
	 * Build the initial workspace expected by the transputer bootstrap-style
	 * first-entry sequence:
	 *   Wptr[0] = synthetic return address
	 *   Wptr[1] = arg0 (fn)
	 *   Wptr[2] = arg1 (arg)
	 *   Wptr[3] = first code address
	 *
	 * Old transputer bootstrap code enters a fresh workspace with
	 *   GAJW; LDNL 2; GCALL
	 * rather than by restoring a previously suspended IPtr. Model new kernel
	 * threads the same way instead of pretending they are already resumable.
	 */
	switch_frame = (unsigned long *)childregs - 4;
	memset(switch_frame, 0, 4 * sizeof(*switch_frame));
	switch_frame[0] = (unsigned long)machine_halt;
	switch_frame[1] = kthread_fn;
	switch_frame[2] = kthread_arg;
	switch_frame[3] = 0;

	childregs->areg = 0; /* child returns 0 from fork */

	/*
	 * Set up the saved context for __switch_to().
	 * Fresh tasks are entered through a dedicated GAJW/LDNL/GCALL path.
	 * Resumed tasks use the normal saved wptr/iptr restore path.
	 */
	p->thread.wptr = (unsigned long)switch_frame;
	p->thread.iptr = 0;
	p->thread.status = TP_THREAD_STATUS_NEW;
	p->thread.ksp = (unsigned long)childregs;

	/* Keep fn/arg in thread_struct too for debugging/state inspection. */
	p->thread.areg = kthread_fn;
	p->thread.breg = kthread_arg;

	return 0;
}

/*
 * __switch_to — context switch between two kernel threads.
 *
 * This is called from the switch_to() macro in asm/system.h.
 * The real work (saving/restoring workspace pointer via GAJW) is done
 * in switch.S. This C wrapper handles the global __current_thread_info
 * pointer update, which can't easily be done in assembly.
 *
 * NOTE: The actual GAJW-based context switch is TODO. For now this
 * performs a simplified cooperative switch that works for the boot
 * sequence by saving/restoring the workspace pointer fields.
 */
extern struct thread_info *__current_thread_info;

/*
 * Assembly context switch helper (in switch.S).
 * Saves prev's WPtr/IPtr, loads next's, does GAJW.
 * On entry: AReg=ret_addr, BReg=prev_thread, CReg=next_thread.
 */
extern void __transputer_context_switch(struct thread_struct *prev_thread,
					struct thread_struct *next_thread);
extern void __transputer_start_thread(struct thread_struct *prev_thread,
				      struct thread_struct *next_thread);

struct task_struct *__switch_to(struct task_struct *prev,
				struct task_struct *next)
{
	struct task_struct *last;

	/* Update global thread_info pointer BEFORE switching stacks.
	 * After GAJW we're on next's stack, so current_thread_info()
	 * must return next's thread_info. */
	__current_thread_info = task_thread_info(next);

	/*
	 * switch_to(prev, next, last) must return the task which most recently
	 * switched away from the task being resumed.  The C activation below is
	 * suspended across GAJW, so its local @prev still names this task when it
	 * eventually resumes and cannot itself describe the later switcher.
	 *
	 * Carry that task pointer in the resumed thread's spare saved CReg slot.
	 * Fresh tasks consume the same handoff in ret_from_fork() for
	 * schedule_tail(); resumed tasks consume it below for
	 * finish_task_switch().
	 */
	next->thread.creg = (unsigned long)prev;

	if (next->thread.status & TP_THREAD_STATUS_NEW) {
		next->thread.status &= ~TP_THREAD_STATUS_NEW;
		__transputer_start_thread(&prev->thread, &next->thread);
	} else {
		/* Resume a previously switched-out kernel task. */
		__transputer_context_switch(&prev->thread, &next->thread);
	}

	/* We reach here only when a later switch resumes @prev. */
	last = (struct task_struct *)prev->thread.creg;
	prev->thread.creg = 0;
	return last;
}

/*
 * Process lifecycle stubs
 */
void release_thread(struct task_struct *dead_task) { }
void flush_thread(void) { }
unsigned long thread_saved_pc(struct task_struct *tsk) { return 0; }
unsigned long __get_wchan(struct task_struct *p) { return 0; }
void free_initrd_mem(unsigned long start, unsigned long end) { }

/* transputer_ldtimer — in switch.S (pure asm: LDTIMER + RET) */
