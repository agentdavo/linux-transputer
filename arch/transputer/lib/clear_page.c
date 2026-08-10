// SPDX-License-Identifier: GPL-2.0
/*
 * clear_page — zero one 4 KiB page with word stores.
 *
 * The old page.S MOVE-doubling clear was fast on T4 but multi-cycle MOVE on
 * SPINE RTL made boot appear stuck in clear_loop (Iptr ~clear_page) for the
 * whole sim TIMEOUT, never reaching setup_arch C/D, THDS, or simplefb.
 *
 * Word stores: 1024 × 32-bit = one page; works with posted SDRAM stores.
 */
#include <linux/types.h>
#include <asm/page.h>

void clear_page_move(void *page)
{
	unsigned long *p = (unsigned long *)page;
	unsigned int i;

	for (i = 0; i < (PAGE_SIZE / sizeof(unsigned long)); i++)
		p[i] = 0;
}
