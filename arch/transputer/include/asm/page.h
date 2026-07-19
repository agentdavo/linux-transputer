/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_PAGE_H
#define _ASM_TRANSPUTER_PAGE_H

#include <linux/const.h>
#include <vdso/page.h>

/* Virtual address where kernel space starts */
#define PAGE_OFFSET	0x00000000UL

#ifndef __ASSEMBLY__

/*
 * Optimized page operations using T800 MOVE instruction.
 * clear_page_move: zeros 4096 bytes using MOVE doubling (~2200 cycles).
 * copy_page_move: copies 4096 bytes using MOVE (~2056 cycles).
 * Both are ~44x faster than byte-by-byte loops.
 * Implemented in arch/transputer/lib/page.S.
 */
extern void clear_page_move(void *page);
extern void copy_page_move(void *to, void *from);

#define clear_page(page)	clear_page_move(page)
#define copy_page(to, from)	copy_page_move(to, from)

#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) })
#define __pmd(x)	((pmd_t) { (x) })
#define __pgd(x)	((pgd_t) { (x) })
#define __pgprot(x)	((pgprot_t) { (x) })

extern unsigned long memory_start;
extern unsigned long memory_end;

/*
 * Transputer RAM starts at 0x80000000 (MinInt), not at 0.
 * ARCH_PFN_OFFSET tells the FLATMEM memory model that PFN space
 * starts at memory_start >> PAGE_SHIFT, so pfn_to_page/page_to_pfn
 * index correctly into the mem_map array.
 */
#define ARCH_PFN_OFFSET		(memory_start >> PAGE_SHIFT)

#define __pa(x)		((unsigned long)(x))
#define __va(x)		((void *)((unsigned long)(x)))

#define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)	__va((pfn) << PAGE_SHIFT)

#define virt_to_page(kaddr)	pfn_to_page(virt_to_pfn(kaddr))
#define page_to_virt(page)	pfn_to_virt(page_to_pfn(page))
#define pfn_valid(pfn)		((pfn) >= ARCH_PFN_OFFSET && \
				 ((pfn) - ARCH_PFN_OFFSET) < max_mapnr)

#define virt_addr_valid(kaddr)	(((unsigned long)(kaddr) >= memory_start) && \
				 ((unsigned long)(kaddr) < memory_end))

#define VM_DATA_DEFAULT_FLAGS	VM_READ | VM_WRITE | VM_EXEC | \
				VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC

#endif /* !__ASSEMBLY__ */

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* _ASM_TRANSPUTER_PAGE_H */
