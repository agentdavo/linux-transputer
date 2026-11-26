// SPDX-License-Identifier: GPL-2.0
/*
 * TinyCC link-time stubs for unreachable generic MM/FS branches.
 *
 * Upstream Linux relies on compiler dead-code elimination of constant-false
 * branches (e.g. !CONFIG_TRANSPARENT_HUGEPAGE, !CONFIG_HUGETLB_PAGE,
 * !CONFIG_FS_DAX) to drop calls to functions that are declared in headers
 * but not compiled.  TinyCC does not fold those branches, so provide
 * unreachable stubs here instead of patching generic mm/ and fs/ files.
 */

#ifdef __TINYC__

#include <linux/bug.h>
#include <linux/dax.h>
#include <linux/gfp.h>
#include <linux/huge_mm.h>
#include <linux/hugetlb.h>
#include <linux/mm.h>

#if !IS_ENABLED(CONFIG_FS_DAX)
int dax_delete_mapping_entry(struct address_space *mapping, pgoff_t index)
{
	BUG();
	return 0;
}
#endif

#ifndef CONFIG_TRANSPARENT_HUGEPAGE
gfp_t vma_thp_gfp_mask(struct vm_area_struct *vma)
{
	BUG();
	return 0;
}
#endif

#ifdef CONFIG_MMU

#ifndef CONFIG_PGTABLE_HAS_HUGE_LEAVES
struct folio *vm_normal_folio_pmd(struct vm_area_struct *vma,
				  unsigned long addr, pmd_t pmd)
{
	BUG();
	return NULL;
}
#endif

#ifndef CONFIG_TRANSPARENT_HUGEPAGE
int copy_huge_pmd(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pmd_t *dst_pmd, pmd_t *src_pmd, unsigned long addr,
		  struct vm_area_struct *dst_vma,
		  struct vm_area_struct *src_vma)
{
	BUG();
	return 0;
}

int copy_huge_pud(struct mm_struct *dst_mm, struct mm_struct *src_mm,
		  pud_t *dst_pud, pud_t *src_pud, unsigned long addr,
		  struct vm_area_struct *vma)
{
	BUG();
	return 0;
}

bool zap_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		  pmd_t *pmd, unsigned long addr)
{
	BUG();
	return false;
}

int zap_huge_pud(struct mmu_gather *tlb, struct vm_area_struct *vma,
		 pud_t *pud, unsigned long addr)
{
	BUG();
	return 0;
}

vm_fault_t do_huge_pmd_wp_page(struct vm_fault *vmf)
{
	BUG();
	return VM_FAULT_SIGBUS;
}

bool huge_pmd_set_accessed(struct vm_fault *vmf)
{
	BUG();
	return false;
}

int change_huge_pmd(struct mmu_gather *tlb, struct vm_area_struct *vma,
		    pmd_t *pmd, unsigned long addr, pgprot_t newprot,
		    unsigned long cp_flags)
{
	BUG();
	return 0;
}
#endif /* !CONFIG_TRANSPARENT_HUGEPAGE */

#ifndef CONFIG_HUGETLB_PAGE
pte_t huge_ptep_get(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	BUG();
	return *ptep;
}

unsigned long huge_pte_dirty(pte_t pte)
{
	BUG();
	return 0;
}
#endif /* !CONFIG_HUGETLB_PAGE */

#endif /* CONFIG_MMU */

#endif /* __TINYC__ */
