// SPDX-License-Identifier: GPL-2.0
#ifndef _ASM_TRANSPUTER_PGTABLE_PAGED_H
#define _ASM_TRANSPUTER_PGTABLE_PAGED_H

#include <asm/page.h>
#include <asm-generic/pgtable-nopmd.h>

#define MAX_POSSIBLE_PHYSMEM_BITS 40
#define PGDIR_SHIFT 21
#define PGDIR_SIZE (1UL << PGDIR_SHIFT)
#define PGDIR_MASK (~(PGDIR_SIZE - 1))
#define PTRS_PER_PGD 2048
#define PTRS_PER_PTE 512
#define PFN_PTE_SHIFT PAGE_SHIFT

#define _PAGE_PRESENT 1UL
#define _PAGE_READ    2UL
#define _PAGE_WRITE   4UL
#define _PAGE_EXEC    8UL
#define _PAGE_YOUNG  16UL
#define _PAGE_DIRTY  32UL

#define PAGE_NONE __pgprot(_PAGE_PRESENT)
#define PAGE_SHARED __pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE)
#define PAGE_COPY __pgprot(_PAGE_PRESENT | _PAGE_READ)
#define PAGE_READONLY PAGE_COPY
#define PAGE_KERNEL __pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_DIRTY | _PAGE_YOUNG)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

#define pmd_none(x) (!pmd_val(x))
#define pmd_present(x) ((pmd_val(x) & _PAGE_PRESENT) != 0)
#define pmd_bad(x) ((pmd_val(x) & ~PAGE_MASK) != _PAGE_PRESENT)
#define set_pmd(p, x) (*(p) = (x))
#define pmd_clear(p) set_pmd(p, __pmd(0))
#define pmd_page_vaddr(x) ((unsigned long)__va(pmd_val(x) & PAGE_MASK))
#define pmd_page(x) pfn_to_page(pmd_val(x) >> PAGE_SHIFT)
#define pmd_pfn(x) (pmd_val(x) >> PAGE_SHIFT)
#define pte_none(x) (!pte_val(x))
#define pte_present(x) (!!(pte_val(x) & _PAGE_PRESENT))
#define pte_write(x) (!!(pte_val(x) & _PAGE_WRITE))
#define pte_dirty(x) (!!(pte_val(x) & _PAGE_DIRTY))
#define pte_young(x) (!!(pte_val(x) & _PAGE_YOUNG))
#define pte_exec(x) (!!(pte_val(x) & _PAGE_EXEC))
#define pte_mkdirty(x) __pte(pte_val(x) | _PAGE_DIRTY)
#define pte_mkyoung(x) __pte(pte_val(x) | _PAGE_YOUNG)
#define pte_mkwrite_novma(x) __pte(pte_val(x) | _PAGE_WRITE)
#define pte_mkclean(x) __pte(pte_val(x) & ~(unsigned long long)_PAGE_DIRTY)
#define pte_mkold(x) __pte(pte_val(x) & ~(unsigned long long)_PAGE_YOUNG)
#define pte_wrprotect(x) __pte(pte_val(x) & ~(unsigned long long)_PAGE_WRITE)
#define pte_pfn(x) ((unsigned long)(pte_val(x) >> PAGE_SHIFT))
#define pfn_pte(pfn, prot) __pte(((unsigned long long)(pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pte_page(x) pfn_to_page(pte_pfn(x))
#define pte_clear(mm, addr, p) set_pte(p, __pte(0))

/* Non-present PTEs carry the soft-leaf payload in bits 32..63. */
#define __swp_type(e) ((e).val & 31UL)
#define __swp_offset(e) ((e).val >> 5)
#define __swp_entry(type, offset) ((swp_entry_t){ (type) | ((offset) << 5) })
#define __pte_to_swp_entry(p) ((swp_entry_t){ (unsigned long)(pte_val(p) >> 32) })
#define __swp_entry_to_pte(e) __pte((unsigned long long)(e).val << 32)
#define pte_swp_exclusive(p) (!!(pte_val(p) & 2))
#define pte_swp_mkexclusive(p) __pte(pte_val(p) | 2)
#define pte_swp_clear_exclusive(p) __pte(pte_val(p) & ~2ULL)

/* Refill is demand-driven. Discard cached translations after publishing PTEs. */
#include <asm/tlbflush.h>
#define update_mmu_cache_range(vmf, vma, addr, ptep, nr) \
	flush_tlb_range_va(addr, (addr) + (nr) * PAGE_SIZE)
#define update_mmu_cache(vma, addr, ptep) flush_tlb_one(addr)

/*
 * A 64-bit leaf on a 32-bit CPU (PAGING.md): the walker must never see a
 * valid low word with a stale high word.  Invalidate the low word, write
 * the high word, then write the low word carrying V; to clear, the low
 * word goes first.  The volatile word stores keep TinyCC from choosing the
 * order.
 */
static inline void set_pte(pte_t *p, pte_t val)
{
	volatile u32 *w = (volatile u32 *)p;
	u32 lo = (u32)pte_val(val), hi = (u32)(pte_val(val) >> 32);

	w[0] = 0;
	barrier();
	w[1] = hi;
	barrier();
	w[0] = lo;
}

static inline pte_t pte_modify(pte_t old, pgprot_t prot)
{
	return __pte((pte_val(old) & (~0xfffULL | _PAGE_DIRTY | _PAGE_YOUNG)) |
		     pgprot_val(prot));
}
#endif
