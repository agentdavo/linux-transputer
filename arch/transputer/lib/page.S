/* SPDX-License-Identifier: GPL-2.0 */
/*
 * copy_page_move(void *to, void *from) — single MOVE of PAGE_SIZE.
 * clear_page is implemented in clear_page.c (word stores; no MOVE doubling).
 *
 * CALL frame: [0]=ret [1]=arg0 [2]=arg1
 */

.text

.globl copy_page_move
copy_page_move:
	.byte 0x72			/* ldl 2 from → C */
	.byte 0x71			/* ldl 1 to → B */
	.byte 0x21, 0x20, 0x20, 0x40	/* ldc 4096 → A */
	.byte 0x24, 0xfa		/* MOVE */
	.byte 0x22, 0xf0		/* ret */
