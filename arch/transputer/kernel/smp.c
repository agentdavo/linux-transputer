// SPDX-License-Identifier: GPL-2.0
/* Transputer shared-kernel SMP. IPIs are polled at kernel safe points and P traps. */
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/bug.h>
#include <linux/atomic.h>
#include <linux/percpu.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <asm/irq_regs.h>
#include <asm/thds.h>
#ifdef CONFIG_MMU
#include <asm/pgm1.h>
#endif
#include <asm/timex.h>

extern int tp_smp_futex_selftest(void);
extern void tp_publish_syscall_gate(void);

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
#define TP_IPI_RESCHEDULE 1u
#define TP_IPI_CALL       2u
#define TP_IPI_STOP       4u
static unsigned int tp_cpu_count = 1;
static struct task_struct *tp_secondary_idle[NR_CPUS];
static unsigned int tp_ipi_active[NR_CPUS];
static unsigned int tp_ipi_ready;
static DEFINE_PER_CPU(unsigned int, tp_callback_count);
extern void transputer_secondary_time_init(void);

/* These words are board RAM, separate from the shared kernel RAM window. */
static atomic_t *tp_ipi_word(unsigned int cpu)
{
	return (atomic_t *)(0x40001000u + 4u * cpu);
}

void __init tp_smp_discover_cpus(void)
{
	volatile unsigned int *ctl = (volatile unsigned int *)0x42000000u;
	unsigned int cpu, count;
	BUG_ON(ctl[0] != 0x534d5031u || ctl[1] != 0);
	count = ctl[2];
	BUG_ON(!count || count > 8);
	tp_cpu_count = min(count, (unsigned int)NR_CPUS);
	for (cpu = 0; cpu < tp_cpu_count; cpu++) {
		set_cpu_possible(cpu, true);
		atomic_set(tp_ipi_word(cpu), 0);
	}
	pr_info("Transputer shared kernel: %u possible CPUs\n", tp_cpu_count);
}

static void tp_send_ipi(unsigned int cpu, unsigned int bits)
{
	BUG_ON(cpu >= tp_cpu_count);
	atomic_or(bits, tp_ipi_word(cpu));
}

/* No asynchronous L-mode interrupt is claimed: cpu_relax, idle, syscall and
* protected timeslice boundaries poll this mailbox with local IRQs enabled.
* Exchange clears only the observed batch; racing senders remain pending.
*/
void tp_smp_poll_ipi(void)
{
	unsigned int cpu, pending;
	unsigned long flags;
	struct pt_regs regs = { 0 }, *saved_regs;
	if (!READ_ONCE(tp_ipi_ready) || arch_irqs_disabled())
		return;
	cpu = raw_smp_processor_id();
	flags = arch_local_irq_save();
	if (tp_ipi_active[cpu]) {
		arch_local_irq_restore(flags);
		return;
	}
	tp_ipi_active[cpu] = 1;
	pending = atomic_xchg(tp_ipi_word(cpu), 0);
	if (pending) {
		saved_regs = set_irq_regs(&regs);
		irq_enter();
		if (pending & TP_IPI_CALL)
			generic_smp_call_function_single_interrupt();
		if (pending & TP_IPI_RESCHEDULE)
			scheduler_ipi();
		irq_exit();
		set_irq_regs(saved_regs);
		if (pending & TP_IPI_STOP) {
			set_cpu_online(cpu, false);
			/* A stopped CPU must not poll or access scheduler state again. */
			for (;;)
				barrier();
		}
	}
	tp_ipi_active[cpu] = 0;
	arch_local_irq_restore(flags);
}

static void __noreturn tp_start_secondary(void)
{
	unsigned int cpu = raw_smp_processor_id();
	struct task_struct *idle = READ_ONCE(tp_secondary_idle[cpu]);
	arch_local_irq_disable();
	transputer_start_timer();
	tp_set_current_thread_info(task_thread_info(idle));
	current_thread_info()->cpu = cpu;
	mmgrab(&init_mm);
	current->active_mm = &init_mm;
#ifdef CONFIG_MMU
	pgm1_secondary_init();
#endif
	(void)tp_thds_boot_install();
	tp_publish_syscall_gate();
	notify_cpu_starting(cpu);
	transputer_secondary_time_init();
	set_cpu_online(cpu, true);
	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	volatile unsigned int *mailbox;
	unsigned long deadline;
	if (!cpu || cpu >= tp_cpu_count || IS_ERR_OR_NULL(idle))
		return -EINVAL;
	mailbox = (volatile unsigned int *)(0x40002000u + cpu * 16u);
	if (mailbox[3])
		return -EBUSY;
	task_thread_info(idle)->cpu = cpu;
	/* Direct secondary entry consumes copy_thread's first-run state. Once
	 * switched out, this idle task must resume its saved kernel context. */
	idle->thread.status = 0;
	WRITE_ONCE(tp_secondary_idle[cpu], idle);
	mailbox[0] = (unsigned long)tp_start_secondary;
	/* Direct C entry has no arguments and never returns. Leave space above
	* the initial workspace for the normal four-word CALL frame convention. */
	mailbox[1] = (unsigned long)task_stack_page(idle) + THREAD_SIZE - 64;
	mailbox[2] = 0;
	smp_wmb();
	mailbox[3] = 1;
	deadline = jiffies + 5 * HZ;
	while (!cpu_online(cpu)) {
		if (time_after(jiffies, deadline)) {
			pr_err("Transputer CPU%u failed to enter Linux\n", cpu);
			return -ETIMEDOUT;
		}
		cpu_relax();
	}
	return 0;
}

static void tp_smp_callback_probe(void *arg)
{
	unsigned int *result = arg;
	unsigned int cpu = raw_smp_processor_id();
	BUG_ON(task_cpu(current) != cpu);
	per_cpu(tp_callback_count, cpu)++;
	*result = cpu + 1;
}
struct tp_worker_probe {
	struct completion ready;
	struct completion done;
	unsigned int cpu;
	int failed;
};
static struct tp_worker_probe tp_worker_probes[NR_CPUS];
static atomic_t tp_worker_count = ATOMIC_INIT(0);
static DECLARE_COMPLETION(tp_worker_start);

static int tp_smp_worker_probe(void *arg)
{
	struct tp_worker_probe *probe = arg;
	unsigned int iteration, word, *buffer;

	/* Sleep until every bound worker exists, including the CPU0 worker. */
	complete(&probe->ready);
	wait_for_completion(&tp_worker_start);

	for (iteration = 0; iteration < 128; iteration++) {
		if (raw_smp_processor_id() != probe->cpu ||
		    task_cpu(current) != probe->cpu) {
			probe->failed = 1;
			break;
		}
		buffer = kmalloc(256, GFP_KERNEL);
		if (!buffer) {
			probe->failed = 1;
			break;
		}
		for (word = 0; word < 64; word++)
			buffer[word] = (probe->cpu << 24) ^ (iteration << 8) ^ word;
		cond_resched();
		for (word = 0; word < 64; word++)
			if (buffer[word] != ((probe->cpu << 24) ^ (iteration << 8) ^ word))
				probe->failed = 1;
		kfree(buffer);
		atomic_inc(&tp_worker_count);
	}
	complete(&probe->done);
	return 0;
}

static int __init tp_smp_workers_test(void)
{
	unsigned int cpu;
	struct task_struct *task;

	for_each_online_cpu(cpu) {
		init_completion(&tp_worker_probes[cpu].ready);
		init_completion(&tp_worker_probes[cpu].done);
		tp_worker_probes[cpu].cpu = cpu;
		task = kthread_create(tp_smp_worker_probe, &tp_worker_probes[cpu],
				      "tp-smp/%u", cpu);
		BUG_ON(IS_ERR(task));
		kthread_bind(task, cpu);
		wake_up_process(task);
	}
	for_each_online_cpu(cpu)
		BUG_ON(!wait_for_completion_timeout(&tp_worker_probes[cpu].ready, 30 * HZ));
	pr_info("Transputer SMP worker start barrier: %u CPUs ready\n", num_online_cpus());
	complete_all(&tp_worker_start);
	for_each_online_cpu(cpu) {
		BUG_ON(!wait_for_completion_timeout(&tp_worker_probes[cpu].done, 30 * HZ));
		BUG_ON(tp_worker_probes[cpu].failed);
	}
	BUG_ON(atomic_read(&tp_worker_count) != 128 * num_online_cpus());
	pr_info("Transputer SMP worker/allocator selftest: PASS cpus=%u allocations=%d\n",
		num_online_cpus(), atomic_read(&tp_worker_count));
	return 0;
}
late_initcall(tp_smp_workers_test);
#else
int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	return -EOPNOTSUPP;
}
#endif

void __init smp_prepare_boot_cpu(void)
{
	current_thread_info()->cpu = 0;
}
void __init smp_prepare_cpus(unsigned int max_cpus)
{
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	unsigned int cpu;
#endif
	/* setup_arch publishes before tp_hard_p is parsed. Refresh CPU0's
	 * private ABI word now; exec may run on a different CPU. */
	tp_publish_syscall_gate();
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	for (cpu = 0; cpu < min(tp_cpu_count, max_cpus); cpu++)
		set_cpu_present(cpu, true);
	WRITE_ONCE(tp_ipi_ready, 1);
#else
	set_cpu_present(0, true);
#endif
#ifndef CONFIG_MMU
	/* passes kernel words as user addresses: NOMMU only */
	BUG_ON(tp_smp_futex_selftest());
#endif
	pr_info("Transputer SMP atomic/futex selftest: PASS\n");
#ifndef CONFIG_TRANSPUTER_SHARED_KERNEL
	pr_info("Transputer SMP build: CPU0 only; shared kernel map/IPIs pending\n");
#endif
}
void __init smp_cpus_done(unsigned int max_cpus)
{
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	unsigned int cpu, result;
	for_each_online_cpu(cpu) {
		result = 0;
		BUG_ON(smp_call_function_single(cpu, tp_smp_callback_probe, &result, 1));
		BUG_ON(result != cpu + 1);
		BUG_ON(per_cpu(tp_callback_count, cpu) != 1);
	}
	pr_info("Transputer SMP cross-CPU callback selftest: PASS\n");
#endif
	pr_info("Transputer SMP: %u Linux CPU online\n", num_online_cpus());
}
void arch_smp_send_reschedule(int cpu)
{
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	tp_send_ipi(cpu, TP_IPI_RESCHEDULE);
#else
	BUG_ON(cpu != raw_smp_processor_id());
	set_tsk_need_resched(current);
#endif
}
void arch_send_call_function_single_ipi(int cpu)
{
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	tp_send_ipi(cpu, TP_IPI_CALL);
#else
	BUG();
#endif
}
void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	unsigned int cpu;
	for_each_cpu(cpu, mask)
		tp_send_ipi(cpu, TP_IPI_CALL);
#else
	BUG_ON(!cpumask_empty(mask));
#endif
}
void smp_send_stop(void)
{
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	unsigned int cpu;
	for_each_online_cpu(cpu)
		if (cpu != raw_smp_processor_id())
			tp_send_ipi(cpu, TP_IPI_STOP);
#else
	BUG_ON(num_online_cpus() != 1);
#endif
}
