/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_TP_SMP_BOOT_H
#define _ASM_TRANSPUTER_TP_SMP_BOOT_H
/* Early bring-up ABI. Shared data only; secondary text remains in private RAM. */
#define TP_BOOT_BASE 0x40000000u
#define TP_BOOT_CTL 0x41000000u
#define TP_BOOT_MAGIC 0x54505331u
#define TP_BOOT_CPUS 4u
#define TP_BOOT_STACK_BASE 0x40010000u
#define TP_BOOT_STACK_SIZE 0x4000u
#define TP_BOOT_ITERATIONS 1000u
#define TP_BOOT_WAIT 20000000u
struct tp_boot_cpu {
    unsigned int release, entered, done, error;
    unsigned int cpu, workspace, stack_low, stack_high;
    unsigned int cookie, observed_cookie, iterations, parked;
};
struct tp_boot_table {
    unsigned int magic, version, cpus, finished;
    unsigned int add_count, cas_count, lock, lock_count;
    struct tp_boot_cpu cpu[TP_BOOT_CPUS];
};
#define TP_BOOT ((volatile struct tp_boot_table *)TP_BOOT_BASE)
#define TP_CTL ((volatile unsigned int *)TP_BOOT_CTL)
unsigned int tp_boot_wptr(void);
unsigned int tp_boot_atomic(volatile unsigned int *p, unsigned int expected,
                            unsigned int operand, unsigned int command);
int tp_boot_primary(void);
void tp_boot_secondary(void);
void tp_boot_park(void);
#endif
