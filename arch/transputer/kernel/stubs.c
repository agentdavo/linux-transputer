// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer architecture-specific stubs
 * Provides minimal implementations for arch-specific functions only.
 *
 * Note: This file should ONLY contain architecture-specific functions.
 * Do NOT add stubs for kernel subsystem functions (fs/mm/block/etc) -
 * those have real implementations in their respective subsystems.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/ptrace.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

/* ========================================================================
 * Machine Control Functions (arch-specific)
 * ======================================================================== */

void machine_restart(char *cmd)
{
	/* Halt the transputer with STOPP instruction */
	__asm__(".byte 0x21, 0xf6");  /* stopp */
	while(1);
}
EXPORT_SYMBOL(machine_restart);

void machine_halt(void)
{
	__asm__(".byte 0x21, 0xf6");  /* stopp */
	while(1);
}
EXPORT_SYMBOL(machine_halt);

void machine_power_off(void)
{
	machine_halt();
}
EXPORT_SYMBOL(machine_power_off);

/* ========================================================================
 * Thread/Task Management — moved to process.c (copy_thread, flush_thread)
 * ======================================================================== */

/*
 * ret_from_fork — entry point for newly created kernel threads.
 *
 * When __switch_to() first dispatches a new task created by copy_thread(),
 * it "returns" to this function. The new task's pt_regs (at the top of
 * its kernel stack) contain fn in areg and arg in breg.
 *
 * TODO: This should be assembly once GAJW context switching is implemented.
 * For now it's C and called from the cooperative __switch_to path.
 */
extern void schedule_tail(struct task_struct *prev);
extern void do_exit(long code);
extern void t9k_enter_userspace(unsigned long entry, unsigned long wptr);

/*
 * ret_from_fork — entry point for newly created kernel threads.
 *
 * Entered via GCALL from switch.S after GAJW switches to the new task's
 * stack. __current_thread_info was already updated by __switch_to(), so
 * current->thread has the fn/arg saved by copy_thread().
 *
 * This function must NEVER return — it calls do_exit() when done.
 */
void ret_from_fork(void)
{
	struct task_struct *prev;
	int (*fn)(void *);
	void *arg;
	int ret;

	/*
	 * Finish the scheduler handoff made by context_switch().  Passing NULL
	 * here is not valid: finish_task_switch() needs the actual previous task
	 * to release the runqueue lock and complete deferred task/MM cleanup.
	 */
	prev = (struct task_struct *)current->thread.creg;
	current->thread.creg = 0;
	schedule_tail(prev);

	/* Get kernel thread function and arg from current task's thread_struct.
	 * These were stored by copy_thread() from the pt_regs prepared by
	 * kernel_thread(). */
	fn = (int (*)(void *))current->thread.areg;
	arg = (void *)current->thread.breg;

	if (fn) {
		ret = fn(arg);
		/* A successful generic kernel_execve() leaves the bFLT entry and
		 * stack in this task's pt_regs.  Normal architectures return through
		 * their assembly user-return path; NOMMU Transputer enters it directly. */
		if (ret == 0) {
			struct pt_regs *regs = current_pt_regs();

			if (user_mode(regs))
				t9k_enter_userspace(regs->iptr, regs->wptr);
		}
		do_exit(ret);
	}

	/* Should never reach here */
	do_exit(0);
}

/* ========================================================================
 * Debug/Trace Functions (arch-specific)
 * ======================================================================== */

void show_regs(struct pt_regs *regs)
{
	printk("Register dump not available on Transputer\n");
}
EXPORT_SYMBOL(show_regs);

void show_stack(struct task_struct *task, unsigned long *sp)
{
	printk("Stack trace not available on Transputer\n");
}
EXPORT_SYMBOL(show_stack);

/* ========================================================================
 * Ptrace Support (arch-specific)
 * ======================================================================== */

void ptrace_disable(struct task_struct *child)
{
	/* No ptrace support */
}

long arch_ptrace(struct task_struct *child, long request,
                 unsigned long addr, unsigned long data)
{
	return -ENOSYS;
}

/* ========================================================================
 * MMU/Paging (arch-specific - no MMU on Transputer)
 * ======================================================================== */

void __weak pgtable_cache_init(void)
{
	/* No MMU - nothing to do */
}

/* ========================================================================
 * Compiler Builtins (TCC-specific)
 * ======================================================================== */

int __weak __builtin_ctzll(unsigned long long x)
{
	int n = 0;
	if (!x) return 64;
	while ((x & 1) == 0) {
		x >>= 1;
		n++;
	}
	return n;
}

int __weak __builtin_clzll(unsigned long long x)
{
	int n = 0;
	if (!x) return 64;
	unsigned long long mask = 1ULL << 63;
	while ((x & mask) == 0) {
		mask >>= 1;
		n++;
	}
	return n;
}

/* ========================================================================
 * Linker Script Symbols (TCC needs these as actual variables)
 * ======================================================================== */

/*
 * These symbols are defined by the linker script (vmlinux.lds.S).
 * Declare them as extern to force the linker to use the script definitions.
 * DO NOT define them here - that would override the linker script values!
 */
extern char _sdata[];
extern char _stext[];
extern char _etext[];
extern char _edata[];
extern char __bss_start[];
extern char __bss_stop[];
extern char _sbss[];
extern char _ebss[];
extern char _end[];
extern char __init_begin[];
extern char __init_end[];
extern char __start_rodata[];
extern char __end_rodata[];
extern char __start_once[];
extern char __end_once[];
extern char __start_notes[];
extern char __stop_notes[];
extern char __start_builtin_fw[];
extern char __end_builtin_fw[];
char __irqentry_text_start[1] __weak = {0};
char __irqentry_text_end[1] __weak = {0};
char __softirqentry_text_start[1] __weak = {0};
char __softirqentry_text_end[1] __weak = {0};
char __cpuidle_text_start[1] __weak = {0};
char __cpuidle_text_end[1] __weak = {0};
char __start___ex_table[1] __weak = {0};
char __stop___ex_table[1] __weak = {0};
char _sinittext[1] __weak = {0};
char _einittext[1] __weak = {0};

void __init clear_bss(void)
{
	/*
	 * NOTE: TCC's linker may not properly resolve linker script symbols.
	 * The emulator typically zeroes memory on load, so BSS clearing may
	 * not be strictly necessary. For now, we skip the clearing loop to
	 * avoid infinite loops caused by unresolved symbols.
	 *
	 * TODO: Either fix TCC linker script symbol support, or provide
	 * the BSS bounds through a different mechanism.
	 */
#if 0
	char *p = __bss_start;
	while (p < __bss_stop)
		*p++ = 0;
#endif
}

/* Console initcall section boundaries — TCC can't populate .con_initcall.init
 * sections via linker script, so we build the table manually here.
 * The loop in console_init() (tty_io.c) iterates from __con_initcall_start
 * to __con_initcall_end calling each function pointer. */
typedef int (*initcall_t)(void);
extern int transputer_console_init(void);
extern int fb_console_init(void);
initcall_t __con_initcall_start[] = {
	transputer_console_init,
	fb_console_init,
};
initcall_t __con_initcall_end[1] = {0};

/* Timer/jiffies */
unsigned long volatile jiffies __weak = 0;
EXPORT_SYMBOL(jiffies);

/* ========================================================================
 * Thread Stack (arch-specific initialization)
 * ======================================================================== */

/* init_stack is now provided by init_task.c via init_thread_union */

/*
 * Link-time assertion stubs — TCC doesn't dead-code-eliminate the default
 * case in switch statements over constants, so these "should never be called"
 * functions get referenced. Provide empty bodies so the linker is happy.
 */
void _NSIG_WORDS_is_unsupported_size(void) { }
void __bad_unaligned_access_size(void) { }
