/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thread_info.h: Transputer thread information
 */
#ifndef _ASM_TRANSPUTER_THREAD_INFO_H
#define _ASM_TRANSPUTER_THREAD_INFO_H

#ifdef __KERNEL__

#include <asm/page.h>

/*
 * TinyCC's Transputer backend uses workspace-resident call frames.  Linux's
 * page allocator reaches slightly beyond an 8 KiB workspace during early
 * boot, corrupting the thread_info at its base.  Keep the usual power-of-two
 * kernel-stack allocation, but provide the headroom required by generated
 * kernel code until frame-slot coalescing is implemented in the backend.
 */
#define THREAD_SIZE_ORDER	2
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#ifndef __ASSEMBLY__

#include <linux/types.h>

/*
 * Note: struct restart_block is defined in linux/thread_info.h before
 * this file is included. Do not include asm/thread_info.h directly —
 * use linux/thread_info.h instead.
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;		/* low level flags */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u32			cpu;		/* always CPU zero */
	unsigned long		addr_limit;	/* legacy Transputer uaccess limit */
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.flags		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.cpu		= 0,			\
	.addr_limit	= TASK_SIZE - 1,	\
	}

/*
 * how to get the thread information struct from C
 *
 * Transputer uses a global pointer rather than SP-masking because:
 * 1. TCC can't emit inline asm to read the workspace pointer
 * 2. TCC does not expose the workspace pointer as a normal C stack pointer
 *    that generic code can mask portably
 * 3. Single-CPU NOMMU, so a global is correct and simplest
 *
 * The pointer is initialized to &init_thread_union.thread_info in
 * init_task.c and updated by schedule() when switching tasks.
 */
extern struct thread_info *__current_thread_info;
static inline struct thread_info *current_thread_info(void)
{
	return __current_thread_info;
}

/* thread information flags */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SINGLESTEP		3	/* restore singlestep on return to user mode */
#define TIF_NOTIFY_RESUME	4	/* callback before returning to user */
#define TIF_RESTORE_SIGMASK	5	/* restore signal mask in do_signal() */
#define TIF_MEMDIE		6	/* is terminating due to OOM killer */
#define TIF_NOTIFY_SIGNAL	7	/* signal notifications exist */

#define _TIF_SYSCALL_TRACE	(1 << TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1 << TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP		(1 << TIF_SINGLESTEP)
#define _TIF_NOTIFY_RESUME	(1 << TIF_NOTIFY_RESUME)
#define _TIF_RESTORE_SIGMASK	(1 << TIF_RESTORE_SIGMASK)
#define _TIF_MEMDIE		(1 << TIF_MEMDIE)

#define _TIF_WORK_MASK		(_TIF_SIGPENDING | _TIF_NEED_RESCHED | \
				 _TIF_NOTIFY_RESUME | _TIF_RESTORE_SIGMASK)

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_TRANSPUTER_THREAD_INFO_H */
