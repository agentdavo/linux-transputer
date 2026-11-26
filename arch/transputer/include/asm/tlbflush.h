#ifndef _ASM_TRANSPUTER_TLBFLUSH_H
#define _ASM_TRANSPUTER_TLBFLUSH_H

#ifdef CONFIG_MMU
#include <asm/pgm1.h>
/* PGM1 (emulator PAGING.md): untagged user and window TLBs in every CPU.
 * FLUSH_ALL and FLUSH_VA drop translations from both banks of the CPU
 * that writes them.  The local_ forms are for mappings only this CPU can
 * use (kmap_local, frame slots); the others must reach every CPU, since
 * the TLBs carry no address-space tag. */
static inline void local_flush_tlb_all(void)
{
	barrier();
	pgm1_write(PGM1_FLUSH_ALL, 1);
	barrier();
}
static inline void local_flush_tlb_one(unsigned long addr)
{
	barrier();
	pgm1_write(PGM1_FLUSH_VA, addr);
	barrier();
}
static inline void local_flush_tlb_range(unsigned long start, unsigned long end)
{
	if (end - start > 64 * PAGE_SIZE) {
		local_flush_tlb_all();
		return;
	}
	for (start &= PAGE_MASK; start < end; start += PAGE_SIZE)
		local_flush_tlb_one(start);
}

#ifdef CONFIG_SMP
/* Shootdown by IPI (mm/pgm1.c): every online CPU runs the local flush. */
void flush_tlb_all(void);
void flush_tlb_one(unsigned long addr);
void flush_tlb_range_va(unsigned long start, unsigned long end);
#else
#define flush_tlb_all()			local_flush_tlb_all()
#define flush_tlb_one(addr)		local_flush_tlb_one(addr)
#define flush_tlb_range_va(start, end)	local_flush_tlb_range(start, end)
#endif

#define flush_tlb_mm(mm) flush_tlb_all()
#define flush_tlb_page(vma, addr) flush_tlb_one(addr)
#define flush_tlb_range(vma, start, end) flush_tlb_range_va(start, end)
#define flush_tlb_kernel_range(start, end) flush_tlb_range_va(start, end)
#else
#include <asm-generic/tlbflush.h>
#endif

#endif
