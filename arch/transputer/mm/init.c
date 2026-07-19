// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer memory management initialization
 * Based on m68k/mm/init_no.c pattern.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
/* From mmu.c, set by setup_arch() */
extern unsigned long memory_start;
extern unsigned long memory_end;

/*
 * Memory initialization for transputer (called from init/main.c mm_init)
 * Based on m68k init_no.c pattern.
 */
void __init mem_init(void)
{
	/* Linux 7's mm_core_init() releases memblock memory centrally. */
}

/*
 * Free memory reserved for initialization
 */
void free_initmem(void)
{
	/* Nothing to do on transputer */
}
