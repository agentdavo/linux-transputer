// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal memory setup for Transputer (NOMMU).
 *
 * Key design: transputer RAM starts at 0x80000000 (MinInt), not 0.
 * ARCH_PFN_OFFSET (defined in asm/page.h) handles the PFN offset so
 * pfn_to_page() works correctly with FLATMEM.
 */

#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <asm/page.h>
#ifdef CONFIG_MMU
#include <asm/pgm1.h>
#endif

/* Set by setup_arch() in setup.c */
unsigned long memory_start = 0x80000000;
unsigned long memory_end = 0x82000000;  /* default 32MiB, updated by setup_arch */

/*
 * Linux 7 initializes zones centrally in mm_core_init_early().  Like the
 * current m68k NOMMU port, the architecture only supplies the end PFN for
 * its single usable zone.
 */
void __init arch_zone_limits_init(unsigned long *max_zone_pfns)
{
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
	max_zone_pfns[ZONE_HIGHMEM] = max_pfn;
#else
	max_zone_pfns[ZONE_NORMAL] = PHYS_PFN(memblock_end_of_DRAM());
#endif
}

#ifdef CONFIG_MMU
pgprot_t vm_get_page_prot(vm_flags_t flags)
{
	unsigned long bits = _PAGE_PRESENT;

	if (flags & VM_READ)
		bits |= _PAGE_READ;
	if (flags & VM_EXEC)
		bits |= _PAGE_EXEC;
	if ((flags & (VM_WRITE | VM_SHARED)) == (VM_WRITE | VM_SHARED))
		bits |= _PAGE_WRITE;
	return __pgprot(bits);
}

pgd_t swapper_pg_dir[PTRS_PER_PGD] __aligned(PTRS_PER_PGD * sizeof(pgd_t));
#endif

void __init paging_init(void)
{
	high_memory = (void *)(memory_end & PAGE_MASK);
#ifdef CONFIG_MMU
	pgm1_paging_init();
#endif
#ifdef CONFIG_TRANSPUTER_PAGING_SELFTEST
	extern void transputer_paging_selftest(void);
	transputer_paging_selftest();
#endif
}
