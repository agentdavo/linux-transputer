/* SPDX-License-Identifier: GPL-2.0 */
/*
 * High memory under PGM1 (emulator PAGING.md).
 *
 * Low memory is the physical RAM the supervisor addresses directly, from
 * 0x80000000 up to memory_end (at most PGM1_LOWMEM_MAX).  Everything else,
 * including all RAM above 4 GiB, is highmem: the kernel reaches a frame
 * only through a PTE in its translated window, kmap_local (fixmap) or
 * kmap (pkmap).  The window TLB caches translations, so every remap is
 * followed by a FLUSH_VA of that address.
 */
#ifndef _ASM_TRANSPUTER_HIGHMEM_H
#define _ASM_TRANSPUTER_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/init.h>
#include <asm/kmap_size.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>

extern pte_t *pkmap_page_table;

#define LAST_PKMAP		512
#define LAST_PKMAP_MASK		(LAST_PKMAP - 1)
#define PKMAP_BASE		0xFF800000UL
#define PKMAP_NR(virt)		(((virt) - PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)		(PKMAP_BASE + ((nr) << PAGE_SHIFT))

#define flush_cache_kmaps()	do { } while (0)

/* kmap_local slots belong to one CPU: a local flush is enough */
#define arch_kmap_local_post_map(vaddr, pteval)	local_flush_tlb_one(vaddr)
#define arch_kmap_local_post_unmap(vaddr)	local_flush_tlb_one(vaddr)

#endif /* __KERNEL__ */

#endif /* _ASM_TRANSPUTER_HIGHMEM_H */
