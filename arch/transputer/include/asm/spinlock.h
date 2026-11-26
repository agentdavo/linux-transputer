/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SPINLOCK_H
#define _ASM_TRANSPUTER_SPINLOCK_H
#include <asm/spinlock_types.h>
#include <asm/barrier.h>
/* Out-of-line implementations avoid recursive always-inline expansion in TCC. */
int arch_spin_trylock(arch_spinlock_t *lock);
void arch_spin_lock(arch_spinlock_t *lock);
void arch_spin_unlock(arch_spinlock_t *lock);
int arch_spin_is_locked(arch_spinlock_t *lock);
int arch_read_trylock(arch_rwlock_t *lock);
void arch_read_lock(arch_rwlock_t *lock);
void arch_read_unlock(arch_rwlock_t *lock);
int arch_write_trylock(arch_rwlock_t *lock);
void arch_write_lock(arch_rwlock_t *lock);
void arch_write_unlock(arch_rwlock_t *lock);
#define arch_spin_is_contended(lock) 0
#endif
