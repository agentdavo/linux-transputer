/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_PGTABLE_H
#define _ASM_TRANSPUTER_PGTABLE_H

#ifdef CONFIG_MMU
#include <asm/pgtable-paged.h>
#else
/* The Transputer is NOMMU; page-table objects are compatibility placeholders. */
#include <asm-generic/pgtable-nopud.h>
#include <asm/page.h>

#define pgd_present(pgd)	(1)
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)		do { } while (0)
#define pmd_offset(a, b)	((void *)0)

#define PAGE_NONE	__pgprot(0)
#define PAGE_SHARED	__pgprot(0)
#define PAGE_COPY	__pgprot(0)
#define PAGE_READONLY	__pgprot(0)
#define PAGE_KERNEL	__pgprot(0)

#define swapper_pg_dir	((pgd_t *)0)

#endif /* CONFIG_MMU */

#ifdef CONFIG_MMU
/* Keep the vmap arena disjoint from direct-mapped RAM and MMIO. */
#define VMALLOC_START 0xf0000000UL
#define VMALLOC_END   0xf8000000UL
#else
#define VMALLOC_START	0UL
#define VMALLOC_END	0xffffffffUL
#endif
#define KMAP_START	0UL
#define KMAP_END		0xffffffffUL

#endif /* _ASM_TRANSPUTER_PGTABLE_H */
