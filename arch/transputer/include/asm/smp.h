/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SMP_H
#define _ASM_TRANSPUTER_SMP_H
#include <linux/cpumask.h>
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
/* Hardware ID must work before current and the per-CPU areas exist. */
#define raw_smp_processor_id() (*(volatile unsigned int *)0x42000004u)
void tp_smp_discover_cpus(void);
void tp_smp_poll_ipi(void);
#else
#define raw_smp_processor_id() (current_thread_info()->cpu)
#endif
void arch_send_call_function_single_ipi(int cpu);
void arch_send_call_function_ipi_mask(const struct cpumask *mask);
#endif
