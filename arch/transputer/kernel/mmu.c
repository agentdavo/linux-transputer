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
	max_zone_pfns[ZONE_NORMAL] = PHYS_PFN(memblock_end_of_DRAM());
}

void __init paging_init(void)
{
	high_memory = (void *)(memory_end & PAGE_MASK);
}
