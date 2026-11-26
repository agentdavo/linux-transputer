// SPDX-License-Identifier: GPL-2.0
/* Early CPU admission test, not Linux scheduler CPU-online support.
 * No kernel globals/libc here: this same object runs on the secondary stub.
 * Only this early, interrupt-masked context may use the per-port command
 * registers without a local lock. General SMP atomics need local exclusion.
 */
#include <asm/tp_smp_boot.h>

unsigned int tp_boot_atomic(volatile unsigned int *p, unsigned int expected,
                            unsigned int operand, unsigned int command)
{
    unsigned int address = (unsigned int)p;
    if ((address & 3) || address < TP_BOOT_BASE || address > TP_BOOT_CTL-4)
        return 0xffffffffu;
    __asm__ __volatile__("" ::: "memory");
    TP_CTL[0] = address;
    TP_CTL[1] = expected;
    TP_CTL[2] = operand;
    TP_CTL[3] = command;
    __asm__ __volatile__("" ::: "memory");
    return TP_CTL[5] ? 0xffffffffu : TP_CTL[4];
}

static int wait_word(volatile unsigned int *word, unsigned int wanted)
{
    unsigned int tries;
    for (tries = 0; tries < TP_BOOT_WAIT; tries++)
        if (*word == wanted) return 0;
    return -1;
}

static unsigned int exercise(unsigned int id)
{
    volatile unsigned int local[256];
    volatile struct tp_boot_cpu *cpu = &TP_BOOT->cpu[id];
    unsigned int i, old, retries, errors = 0;
    cpu->workspace = tp_boot_wptr();
    cpu->cpu = id;
    cpu->observed_cookie = cpu->cookie;
    for (i = 0; i < 256; i++) local[i] = (id << 24) ^ i ^ 0x5a5a0000u;
    cpu->entered = 1;
    for (i = 0; i < TP_BOOT_CPUS; i++)
        if (wait_word(&TP_BOOT->cpu[i].entered, 1)) return 1;
    for (i = 0; i < TP_BOOT_ITERATIONS; i++) {
        if (tp_boot_atomic(&TP_BOOT->add_count, 0, 1, 2) >= TP_BOOT_CPUS*TP_BOOT_ITERATIONS)
            errors++;
        for (retries = 0; retries < TP_BOOT_WAIT; retries++) {
            old = TP_BOOT->cas_count;
            if (tp_boot_atomic(&TP_BOOT->cas_count, old, old+1, 1) == old) break;
        }
        if (retries == TP_BOOT_WAIT) return errors+1;
        for (retries = 0; retries < TP_BOOT_WAIT; retries++)
            if (!tp_boot_atomic(&TP_BOOT->lock, 0, 1, 3)) break;
        if (retries == TP_BOOT_WAIT) return errors+1;
        TP_BOOT->lock_count++;
        tp_boot_atomic(&TP_BOOT->lock, 0, 0, 3);
        cpu->iterations = i+1;
    }
    for (i = 0; i < 256; i++)
        if (local[i] != ((id << 24) ^ i ^ 0x5a5a0000u)) errors++;
    return errors;
}

void tp_boot_secondary(void)
{
    unsigned int id = TP_CTL[6];
    volatile struct tp_boot_cpu *cpu;
    if (!id || id >= TP_BOOT_CPUS || TP_CTL[7] != TP_BOOT_CPUS) tp_boot_park();
    cpu = &TP_BOOT->cpu[id];
    /* Linux owns publication of the table and release words. */
    if (wait_word(&TP_BOOT->magic, TP_BOOT_MAGIC) || wait_word(&cpu->release, 1))
        tp_boot_park();
    if (TP_BOOT->version != 1 || TP_BOOT->cpus != TP_BOOT_CPUS) tp_boot_park();
    cpu->error = exercise(id);
    cpu->done = 1;
    if (wait_word(&TP_BOOT->finished, 1)) tp_boot_park();
    cpu->parked = 1;
    tp_boot_park();
}

int tp_boot_primary(void)
{
    unsigned int i, n, errors = 0;
    volatile unsigned int *words = (volatile unsigned int *)TP_BOOT;
    if (TP_CTL[6] || TP_CTL[7] != TP_BOOT_CPUS) return -1;
    for (i = 0; i < sizeof(*TP_BOOT)/4; i++) words[i] = 0;
    TP_BOOT->version = 1;
    TP_BOOT->cpus = TP_BOOT_CPUS;
    for (i = 0; i < TP_BOOT_CPUS; i++) {
        TP_BOOT->cpu[i].cookie = 0x12340000u+i;
        if (i) {
            unsigned int low = TP_BOOT_STACK_BASE + (i-1)*TP_BOOT_STACK_SIZE;
            TP_BOOT->cpu[i].stack_low = low;
            TP_BOOT->cpu[i].stack_high = low+TP_BOOT_STACK_SIZE;
            for (n = 0; n < 16; n++) ((volatile unsigned int *)low)[n] = 0xc0ffee00u+i;
        }
    }
    __asm__ __volatile__("" ::: "memory");
    TP_BOOT->magic = TP_BOOT_MAGIC;
    for (i = 1; i < TP_BOOT_CPUS; i++) TP_BOOT->cpu[i].release = 1;
    TP_BOOT->cpu[0].error = exercise(0);
    TP_BOOT->cpu[0].done = 1;
    for (i = 0; i < TP_BOOT_CPUS; i++) {
        volatile struct tp_boot_cpu *cpu = &TP_BOOT->cpu[i];
        if (wait_word(&cpu->done, 1)) return -2;
        errors += cpu->error;
        if (cpu->cpu != i || cpu->cookie != cpu->observed_cookie ||
            cpu->iterations != TP_BOOT_ITERATIONS) errors++;
        if (i) {
            if (cpu->workspace < cpu->stack_low+64 || cpu->workspace >= cpu->stack_high)
                errors++;
            for (n = 0; n < 16; n++)
                if (((volatile unsigned int *)cpu->stack_low)[n] != 0xc0ffee00u+i) errors++;
        }
    }
    if (TP_BOOT->add_count != 4000 || TP_BOOT->cas_count != 4000 ||
        TP_BOOT->lock_count != 4000 || TP_BOOT->lock) errors++;
    if (tp_boot_atomic(&TP_BOOT->cas_count, 0, 5, 1) != 4000 ||
        TP_BOOT->cas_count != 4000) errors++;
    TP_BOOT->finished = 1;
    for (i = 1; i < TP_BOOT_CPUS; i++)
        if (wait_word(&TP_BOOT->cpu[i].parked, 1)) return -3;
    return errors ? -4 : 0;
}
