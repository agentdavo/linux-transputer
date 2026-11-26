/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_PROCESSOR_H
#define _ASM_TRANSPUTER_PROCESSOR_H

#include <linux/types.h>
#include <asm/ptrace.h>
#include <asm/segment.h>
#include <asm/tp_softprot.h>


/* CPU specific flags */
#define CPU_ARCH_TRANSPUTER	0x1

/* Stack layout and sizes */
#define KERNEL_STACK_SIZE	PAGE_SIZE

/*
 * User space process size limits.
 *
 * On NOMMU transputer, user and kernel share the full 32-bit address
 * space.  bFLT binaries are loaded via alloc_pages() which returns
 * addresses above 0x80000000 (MemStart).  TASK_SIZE must cover the
 * entire space so that do_getname() / access_ok() don't reject valid
 * user pointers with EFAULT.  (h8300, another NOMMU arch, uses the
 * same 0xFFFFFFFF value.)
 */
#ifdef CONFIG_MMU
#define TASK_SIZE (0x80000000UL)
#define STACK_TOP TASK_SIZE
#define STACK_TOP_MAX STACK_TOP
#else
#define TASK_SIZE		(0xFFFFFFFFUL)
#endif
#ifdef CONFIG_MMU
/* Page aligned: TASK_SIZE / 3 is not, and bottom-up get_unmapped_area()
 * returns the base itself, which mmap then rejects with -EINVAL. */
#define TASK_UNMAPPED_BASE	0x20000000UL
#else
#define TASK_UNMAPPED_BASE	(TASK_SIZE / 3)
#endif

struct tp_spawn_mmap_scope;
struct tp_syscall_state;

/* Thread context */
struct thread_struct {
	unsigned long wptr;	/* Workspace pointer */
	unsigned long iptr;	/* Instruction pointer */
	unsigned long areg;	/* A register */
	unsigned long breg;	/* B register */
	unsigned long creg;	/* C register */
	unsigned long status;	/* Status register */
	
	/* Saved kernel stack pointer */
	unsigned long ksp;

	/* Per-task soft L/P region shadow (Phase 4 soft-mm). */
	struct tp_soft_mm soft_mm;
	struct tp_spawn_mmap_scope *spawn_mmaps;
	struct tp_syscall_state *syscall_state;
	unsigned long user_tp;
};

#define INIT_THREAD  { \
	.wptr = 0, \
	.iptr = 0, \
	.areg = 0, \
	.breg = 0, \
	.creg = 0, \
	.status = 0, \
	.ksp = 0, \
	.soft_mm = { .flags = 0 }, \
}

/* CPU idle */
extern void transputer_poll_tick(void);

static inline void cpu_relax(void)
{
	transputer_poll_tick();
	barrier();
}

/* Prepare to copy thread state */
static inline void prepare_to_copy(struct task_struct *tsk)
{
}

/* CPU ID is handled by generic code through raw_smp_processor_id() */

/* Architecture name string */
#define ELF_PLATFORM "transputer"

/* Set up thread state for exec — called by binfmt_flat after loading */
#define start_thread(regs, entry, usp) do {	\
	set_fs(USER_DS);			\
	(regs)->iptr = (entry);			\
	(regs)->wptr = (usp);			\
	(regs)->areg = 0;			\
	(regs)->breg = 0;			\
	(regs)->creg = 0;			\
	(regs)->status = 0x1;			\
	TP_EXEC_FINISH(regs);			\
} while (0)
#ifdef CONFIG_MMU
void pgm1_exec_setup(struct pt_regs *regs);
#define TP_EXEC_FINISH(regs)	pgm1_exec_setup(regs)
#else
#define TP_EXEC_FINISH(regs)	tp_on_exec_finish(regs)
#endif

/* Get kernel stack EIP for a task */
#define KSTK_EIP(tsk) (task_pt_regs(tsk)->iptr)

/* Get kernel stack ESP for a task */
#define KSTK_ESP(tsk) (task_pt_regs(tsk)->wptr)

/* Forward declaration */
struct task_struct;
void tp_spawn_release_task(struct task_struct *task);

/* Get the wait channel for a sleeping task */
unsigned long __get_wchan(struct task_struct *p);

#endif /* _ASM_TRANSPUTER_PROCESSOR_H */
