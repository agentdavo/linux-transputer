// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/sched/task.h>	/* extern struct task_struct init_task */

/*
 * vmlinux.lds normally creates init_stack via INIT_TASK_DATA(), which places
 * init_thread_info at the *base* of the THREAD_SIZE init_stack region so that
 *
 *     task_thread_info(&init_task) == current_thread_info()
 *
 * (task_thread_info() is (struct thread_info *)init_task.stack, and
 * init_task.stack == init_stack).  The compact Transputer linker does not
 * consume INIT_TASK_DATA(), so we reproduce that layout here by hand: a
 * THREAD_SIZE union whose base holds an initialised thread_info, with the
 * init_stack symbol aliased onto it.  head.S loads WPtr near the top
 * (init_stack + THREAD_SIZE - 64) and the workspace grows downward toward,
 * but does not reach, the thread_info at offset 0.
 *
 * Getting this right is essential: sched_init()->init_idle()->
 * init_idle_preempt_count() resets task_thread_info(&init_task)->preempt_count
 * from INIT_PREEMPT_COUNT (1) to PREEMPT_DISABLED (0), and current_thread_info()
 * must observe that *same* field.  If the two thread_info objects differ, the
 * reset is invisible, preempt_count stays at 1, and the first schedule() in
 * rest_init() aborts with "BUG: scheduling while atomic".
 */
union thread_union transputer_init_thread_union
	__aligned(THREAD_SIZE) = {
	.thread_info = INIT_THREAD_INFO(init_task),
};

unsigned long init_stack[THREAD_SIZE / sizeof(unsigned long)]
	__attribute__((alias("transputer_init_thread_union")));

/*
 * Generic Linux now owns init_task.  The Transputer still needs a directly
 * addressable current-thread pointer because its workspace pointer cannot be
 * recovered by masking a conventional C stack.  Point it at the thread_info
 * that lives at the base of init_stack (== task_thread_info(&init_task)).
 */
struct thread_info *__current_thread_info =
	&transputer_init_thread_union.thread_info;
EXPORT_SYMBOL(__current_thread_info);
