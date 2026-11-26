/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fixed supervisor-window addresses (PGM1, emulator PAGING.md).
 *
 * The window at PGM1_KWIN_BASE is translated through swapper_pg_dir;
 * its leaf tables all exist from boot (pgm1_paging_init()), so a fixmap
 * PTE is always present to write.  Layout of the top of the window:
 *
 *   0xF0000000 .. 0xF8000000  vmalloc / ioremap
 *   0xFF800000 .. 0xFFA00000  pkmap (kmap(), one leaf table)
 *   FIXADDR_START .. 0xFFBFF000  fixmap: kmap_local slots
 *   0xFFC00000 .. +64 KiB     pgm1 frame slots
 */
#ifndef _ASM_TRANSPUTER_FIXMAP_H
#define _ASM_TRANSPUTER_FIXMAP_H

#include <asm/page.h>
#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <asm/kmap_size.h>
#endif

#define FIXADDR_TOP	0xFFBFF000UL

enum fixed_addresses {
#ifdef CONFIG_HIGHMEM
	FIX_KMAP_BEGIN,
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_MAX_IDX * NR_CPUS) - 1,
#endif
	__end_of_fixed_addresses
};

#define FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

#define FIXMAP_PAGE_NORMAL	PAGE_KERNEL
#define FIXMAP_PAGE_IO		PAGE_KERNEL

#include <asm-generic/fixmap.h>

#endif /* _ASM_TRANSPUTER_FIXMAP_H */
