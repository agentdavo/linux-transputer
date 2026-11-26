#ifndef _ASM_TRANSPUTER_PGALLOC_H
#define _ASM_TRANSPUTER_PGALLOC_H

#include <asm-generic/pgalloc.h>

#ifdef CONFIG_MMU
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return __pgd_alloc(mm, 1);
}

#define pmd_populate_kernel(mm, pmd, table) \
	set_pmd(pmd, __pmd(__pa(table) | _PAGE_PRESENT))
#define pmd_populate(mm, pmd, page) \
	set_pmd(pmd, __pmd((page_to_pfn(page) << PAGE_SHIFT) | _PAGE_PRESENT))
#define __pte_free_tlb(tlb, page, addr) \
	tlb_remove_ptdesc(tlb, page_ptdesc(page))
#endif

#endif
