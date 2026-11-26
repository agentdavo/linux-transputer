/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_CACHE_H
#define _ASM_TRANSPUTER_CACHE_H

/* Transputer cache line size - no cache on most models */
#define L1_CACHE_SHIFT	5	/* 32 bytes for alignment purposes */
#define L1_CACHE_BYTES	(1 << L1_CACHE_SHIFT)

/* For SMP cache alignment (not used on UP Transputer) */
#define SMP_CACHE_BYTES	L1_CACHE_BYTES

/* Cache alignment */
#define __cacheline_aligned __attribute__((__aligned__(L1_CACHE_BYTES)))
#define ____cacheline_aligned __attribute__((__aligned__(L1_CACHE_BYTES)))

/* Transputer doesn't have traditional cache */
#define ARCH_DMA_MINALIGN	L1_CACHE_BYTES

/* Prefetch support - no-op since no cache.
 * Must be macros, not static inline (TCC doesn't inline).
 * Used in list_for_each_entry comma expressions. */
#define ARCH_HAS_PREFETCH
#define prefetch(x) ((void)0)
#define ARCH_HAS_PREFETCHW
#define prefetchw(x) ((void)0)

#endif /* _ASM_TRANSPUTER_CACHE_H */
