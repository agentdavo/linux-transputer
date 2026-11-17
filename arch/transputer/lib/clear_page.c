// SPDX-License-Identifier: GPL-2.0
/*
 * clear_page — zero one 4 KiB page with word stores.
 *
 * The old page.S MOVE-doubling clear was fast on T4 but multi-cycle MOVE on
 * SPINE RTL made boot appear stuck in clear_loop (Iptr ~clear_page) for the
 * whole sim TIMEOUT, never reaching setup_arch C/D, THDS, or simplefb.
 *
 * Word stores: 1024 × 32-bit = one page; works with posted SDRAM stores.
 * Sixteen stores per pass use all single-byte STNL word offsets (0..15).
 */
#include <linux/types.h>
#include <asm/page.h>

void clear_page_move(void *page)
{
	unsigned long *p = (unsigned long *)page;
	unsigned long *end = p + PAGE_SIZE / sizeof(unsigned long);

	do {
		p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0;
		p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0;
		p[8] = 0; p[9] = 0; p[10] = 0; p[11] = 0;
		p[12] = 0; p[13] = 0; p[14] = 0; p[15] = 0;
		p += 16;
	} while (p != end);
}
