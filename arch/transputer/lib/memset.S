/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Correct word+tail memset for Transputer bring-up.
 *
 * void *memset(void *s, int c, size_t count)
 *
 * Workspace after AJW -5:
 *   WPtr[0] = original dest
 *   WPtr[1] = current ptr
 *   WPtr[2] = byte value
 *   WPtr[3] = remaining count
 *   WPtr[4] = word pattern
 *   WPtr[5] = return IPtr
 *   WPtr[6] = param0 (dest)
 *   WPtr[7] = param1 (c)
 *   WPtr[8] = param2 (count)
 *
 * Note: mm_zero_struct_page() on this arch is unrolled (see mm.h 32-bit
 * path) so memmap_init no longer depends on this for every struct page.
 */

.text
.globl memset

memset:
		.byte 0x60			/* nfix 0 */
		.byte 0xbb			/* ajw -5 */
		.byte 0x76			/* ldl 6 -> dest */
		.byte 0xd0			/* stl 0 */
		.byte 0x76			/* ldl 6 -> dest */
		.byte 0xd1			/* stl 1 */
		.byte 0x77			/* ldl 7 -> c */
		.byte 0xd2			/* stl 2 */
		.byte 0x78			/* ldl 8 -> count */
		.byte 0xd3			/* stl 3 */

		/* Build c | (c<<8) | (c<<16) | (c<<24) */
		.byte 0x72			/* ldl 2 */
		.byte 0x2f, 0x4f		/* ldc 0xff */
		.byte 0x24, 0xf6		/* and */
		.byte 0xd4			/* stl 4 */
		.byte 0x74			/* ldl 4 */
		.byte 0x48			/* ldc 8 */
		.byte 0x24, 0xf1		/* shl */
		.byte 0x74			/* ldl 4 */
		.byte 0x24, 0xfb		/* or */
		.byte 0xd4			/* stl 4 */
		.byte 0x74			/* ldl 4 */
		.byte 0x21, 0x40		/* ldc 16 */
		.byte 0x24, 0xf1		/* shl */
		.byte 0x74			/* ldl 4 */
		.byte 0x24, 0xfb		/* or */
		.byte 0xd4			/* stl 4 */

	memset_word_loop:
		.byte 0x73			/* ldl 3 */
		.byte 0x43			/* ldc 3 */
		.byte 0xf9			/* gt */
		.byte 0xa1			/* cj +1 -> skip jump when count > 3 */
		.byte 0x0e			/* j +14 -> memset_byte_loop */
		.byte 0x71			/* ldl 1 */
		.byte 0x74			/* ldl 4 */
		.byte 0x20, 0xf0		/* rev */
		.byte 0xe0			/* stnl 0 */
		.byte 0x71			/* ldl 1 */
		.byte 0x84			/* adc 4 */
		.byte 0xd1			/* stl 1 */
		.byte 0x73			/* ldl 3 */
		.byte 0x60, 0x8c		/* adc -4 */
		.byte 0xd3			/* stl 3 */
		.byte 0x61, 0x0d		/* j -19 -> memset_word_loop */

	memset_byte_loop:
		.byte 0x73			/* ldl 3 */
		.byte 0xc0			/* eqc 0 */
		.byte 0xaf			/* cj +15 -> memset_done */
		.byte 0x71			/* ldl 1 */
		.byte 0x72			/* ldl 2 */
		.byte 0x20, 0xf0		/* rev */
		.byte 0x23, 0xfb		/* sb */
		.byte 0x71			/* ldl 1 */
		.byte 0x81			/* adc 1 */
		.byte 0xd1			/* stl 1 */
		.byte 0x73			/* ldl 3 */
		.byte 0x60, 0x8f		/* adc -1 */
		.byte 0xd3			/* stl 3 */
		.byte 0x61, 0x0e		/* j -18 -> memset_byte_loop */

	memset_done:
		.byte 0x70			/* ldl 0 */
		.byte 0xb5			/* ajw 5 */
		.byte 0x22, 0xf0		/* ret */
