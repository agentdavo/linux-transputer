// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/bug.h>
#include <asm/irqflags.h>
#include <asm/cmpxchg.h>
#include <asm/spinlock.h>

#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
extern void tp_smp_poll_ipi(void);
#endif
#ifdef CONFIG_MMU
#include <linux/mm.h>
#include <asm/pgm1.h>
#endif

/* Contended locks are an interrupt-delivery boundary on the polled platform
 * controller. Poll the IPI source without advancing the timer, and never turn
 * an IRQ-off lock acquisition into an interruptible region. The dispatcher guards
 * against recursive entry while an IPI handler itself waits for a lock.
 */
static void tp_lock_wait(void)
{
    __mb();
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
    if (!arch_irqs_disabled())
        tp_smp_poll_ipi();
#endif
}

/* Private RAM belongs to this CPU. Shared board words use the serialized
 * command engine. No secondary may run Linux with a private copy of globals.
 * IRQ exclusion covers the entire per-port operand/command/result sequence.
 */
static unsigned int shared_cas(volatile unsigned int *p, unsigned int old, unsigned int new)
{
    volatile unsigned int *ctl = (volatile unsigned int *)0x41000000u;
    ctl[0] = (unsigned int)p;
    ctl[1] = old;
    ctl[2] = new;
    ctl[3] = 1;
    BUG_ON(ctl[5]);
    return ctl[4];
}

#ifdef CONFIG_MMU
/* Under PGM1 low memory runs to high_memory, not RAM_MIB, and the window
 * at PGM1_KWIN_BASE reaches any frame (vmalloc, kmap, the high bank).
 * The board engine takes a 32-bit physical address, so a window word in
 * RAM0 is compared and swapped at its physical address.  A frame the
 * engine cannot reach (above 4 GiB) is updated under one board-atomic
 * lock word: every atomic on such a frame goes through it. */
static unsigned int tp_far_lock;

static unsigned long tp_shared_target(unsigned long address)
{
    u64 pa;

    if (address >= 0x80000070u && address < (unsigned long)high_memory)
        return address;
    if (address < PGM1_KWIN_BASE || !pgm1_present())
        return 0;
    pa = pgm1_window_pa(address);
    if (pa >= 0x80000070u && pa < 0x100000000ULL)
        return (unsigned long)pa;
    return 0;
}

static unsigned long tp_far_cmpxchg(volatile void *p, unsigned long old,
                                    unsigned long new, unsigned int size)
{
    unsigned long result = 0;

    while (shared_cas(&tp_far_lock, 0, 1) != 0)
        ;
    switch (size) {
    case 1: result = *(volatile u8 *)p; if (result == (u8)old) *(volatile u8 *)p = new; break;
    case 2: result = *(volatile u16 *)p; if (result == (u16)old) *(volatile u16 *)p = new; break;
    case 4: result = *(volatile u32 *)p; if (result == old) *(volatile u32 *)p = new; break;
    }
    __mb();
    (void)shared_cas(&tp_far_lock, 1, 0);
    return result;
}
#endif

unsigned long tp_smp_cmpxchg(volatile void *p, unsigned long old,
                            unsigned long new, unsigned int size)
{
    unsigned long flags = arch_local_irq_save(), address = (unsigned long)p;
    unsigned long result = 0, mask, shift, before, after;
    volatile unsigned int *word;
    int shared = 0;

    BUG_ON((size != 1 && size != 2 && size != 4) || (address & (size-1)));
    __mb();
#ifdef CONFIG_MMU
    if (!(address >= 0x40000000u && address < 0x41000000u)) {
        unsigned long target = tp_shared_target(address);

        if (target) {
            address = target;
            shared = 1;
        } else if (address >= PGM1_KWIN_BASE && pgm1_present()) {
            result = tp_far_cmpxchg(p, old, new, size);
            __mb();
            arch_local_irq_restore(flags);
            return result;
        }
    }
#endif
    if (shared || (address >= 0x40000000u && address < 0x41000000u)
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
        || (address >= 0x80000070u && address < 0x80000000u+CONFIG_TRANSPUTER_RAM_MIB*1024u*1024u)
#endif
    ) {
        word = (volatile unsigned int *)(address & ~3u);
        shift = (address & 3u)*8;
        mask = size == 4 ? ~0u : ((1u << (size*8))-1);
        before = *word;
        for (;;) {
            result = (before >> shift) & mask;
            if (result != (old & mask)) break;
            after = shared_cas(word, before, (before & ~(mask << shift)) | ((new & mask) << shift));
            if (after == before) break;
            before = after;
        }
    } else {
        /* This path is CPU-local, never an inter-CPU lock fallback. */
        BUG_ON(address < 0x80000000u);
        switch (size) {
        case 1: result = *(volatile u8 *)p; if (result == (u8)old) *(volatile u8 *)p = new; break;
        case 2: result = *(volatile u16 *)p; if (result == (u16)old) *(volatile u16 *)p = new; break;
        case 4: result = *(volatile u32 *)p; if (result == old) *(volatile u32 *)p = new; break;
        }
    }
    __mb();
    arch_local_irq_restore(flags);
    return result;
}

unsigned long tp_smp_xchg(volatile void *p, unsigned long new, unsigned int size)
{
    unsigned long old, seen;
    /* A failed CAS with identical expected/new probes without modifying. */
    old = tp_smp_cmpxchg(p, 0, 0, size);
    for (;;) {
        seen = tp_smp_cmpxchg(p, old, new, size);
        if (seen == old) return old;
        old = seen;
    }
}
int arch_spin_trylock(arch_spinlock_t *lock) { return arch_cmpxchg(&lock->value, 0, 1) == 0; }
void arch_spin_lock(arch_spinlock_t *lock)
{
    for (;;) {
        while (lock->value) tp_lock_wait();
        if (arch_spin_trylock(lock)) return;
        tp_lock_wait();
    }
}
void arch_spin_unlock(arch_spinlock_t *lock) { __mb(); lock->value = 0; }
int arch_spin_is_locked(arch_spinlock_t *lock) { return lock->value != 0; }
int arch_read_trylock(arch_rwlock_t *lock)
{
    unsigned int old = lock->value;
    if (old & 0x80000000u) return 0;
    return arch_cmpxchg(&lock->value, old, old+1) == old;
}
void arch_read_lock(arch_rwlock_t *lock) { while (!arch_read_trylock(lock)) tp_lock_wait(); }
void arch_read_unlock(arch_rwlock_t *lock)
{
    unsigned int old;
    do { old = lock->value; } while (arch_cmpxchg(&lock->value, old, old-1) != old);
}
int arch_write_trylock(arch_rwlock_t *lock) { return arch_cmpxchg(&lock->value, 0, 0x80000000u) == 0; }
void arch_write_lock(arch_rwlock_t *lock) { while (!arch_write_trylock(lock)) tp_lock_wait(); }
void arch_write_unlock(arch_rwlock_t *lock) { __mb(); lock->value = 0; }
