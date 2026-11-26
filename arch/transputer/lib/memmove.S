/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer assembly memmove implementation using MOVE instruction
 *
 * void *memmove(void *dest, const void *src, size_t count)
 *
 * When dest <= src: forward copy using MOVE instruction (#4A).
 *   MOVE copies at word speed (~2w+8 cycles), ~44x faster than SB loop.
 *   Non-overlapping in forward direction is guaranteed when dest <= src.
 *
 * When dest > src: backward byte-by-byte copy to handle overlap safely.
 *   MOVE cannot be used because it is undefined for overlapping regions.
 *
 * TCWG calling convention after CALL:
 *   WPtr[+0]: Return IPtr
 *   WPtr[+4]: param0 (dest)
 *   WPtr[+8]: param1 (src)
 *   WPtr[+12]: param2 (count)
 *
 * Returns: dest (original destination pointer)
 *
 * Workspace layout after AJW -5:
 *   WPtr[0]: local0 - saved original dest (return value)
 *   WPtr[1]: local1 - current dest ptr (backward loop)
 *   WPtr[2]: local2 - current src ptr (backward loop)
 *   WPtr[3]: local3 - count remaining (backward loop)
 *   WPtr[4]: (unused)
 *   WPtr[5]: return IPtr
 *   WPtr[6]: param0 (dest)
 *   WPtr[7]: param1 (src)
 *   WPtr[8]: param2 (count)
 *
 * Byte map:
 *
 * OFF  HEX    INSTRUCTION
 *  0   60     nfix 0
 *  1   BB     ajw -5
 *  2   76     ldl 6 (dest)
 *  3   D0     stl 0 (save return value)
 *  4   78     ldl 8 (count)
 *  5   23 A3  cj 51 -> OFF 58 (done) if count==0
 *  7   76     ldl 6 (dest)
 *  8   77     ldl 7 (src) -> AReg=src, BReg=dest
 *  9   F9     gt -> (dest > src) ? 1 : 0
 * 10   22 A9  cj 41 -> OFF 53 (forward) if dest<=src
 * --- backward setup ---
 * 12   76     ldl 6 (dest)
 * 13   78     ldl 8 (count)
 * 14   25 F2  sum -> dest+count
 * 16   60 8F  adc -1 -> dest+count-1
 * 18   D1     stl 1 (dest_end)
 * 19   77     ldl 7 (src)
 * 20   78     ldl 8 (count)
 * 21   25 F2  sum -> src+count
 * 23   60 8F  adc -1 -> src+count-1
 * 25   D2     stl 2 (src_end)
 * 26   78     ldl 8 (count)
 * 27   D3     stl 3 (remaining)
 * --- backward byte loop ---
 * 28   73     ldl 3 (count)
 * 29   21 A5  cj 21 -> OFF 52 (bwd_done)
 * 31   71     ldl 1 (dest_end)
 * 32   72     ldl 2 (src_end) -> AReg=src, BReg=dest
 * 33   F1     lb -> AReg=byte, BReg=dest
 * 34   20 F0  rev -> AReg=dest, BReg=byte
 * 36   23 FB  sb -> store byte
 * 38   72     ldl 2 (src_end)
 * 39   60 8F  adc -1
 * 41   D2     stl 2 (src_end--)
 * 42   71     ldl 1 (dest_end)
 * 43   60 8F  adc -1
 * 45   D1     stl 1 (dest_end--)
 * 46   73     ldl 3 (count)
 * 47   60 8F  adc -1
 * 49   D3     stl 3 (count--)
 * 50   61 08  j -24 -> OFF 28 (backward_loop)
 * --- backward done: skip forward ---
 * 52   05     j 5 -> OFF 58 (done)
 * --- forward copy using MOVE ---
 * 53   77     ldl 7 (src -> CReg)
 * 54   76     ldl 6 (dest -> BReg)
 * 55   78     ldl 8 (count -> AReg)
 * 56   24 FA  MOVE (opr 0x4A)
 * --- epilogue ---
 * 58   70     ldl 0 (return value)
 * 59   B5     ajw 5
 * 60   22 F0  ret
 */

.text
.globl memmove

memmove:
	/* === Prologue === */
	.byte 0x60			/* nfix 0 */
	.byte 0xbb			/* ajw -5 */

	/* Save original dest for return */
	.byte 0x76			/* ldl 6 (dest) */
	.byte 0xd0			/* stl 0 */

	/* Early exit if count == 0 */
	.byte 0x78			/* ldl 8 (count) */
	.byte 0x23, 0xa3		/* cj 51 -> memmove_done */

	/* Direction check: GT tests BReg > AReg */
	/* Load dest, then src: AReg=src, BReg=dest. GT -> (dest > src) */
	/* CJ: if dest <= src (AReg==0), jump to forward MOVE path */
	.byte 0x76			/* ldl 6 (dest) */
	.byte 0x77			/* ldl 7 (src) */
	.byte 0xf9			/* gt -> (dest > src) ? 1 : 0 */
	.byte 0x22, 0xa9		/* cj 41 -> forward_move */

	/* === BACKWARD COPY (dest > src, may overlap) === */
	/* dest_end = dest + count - 1 */
	.byte 0x76			/* ldl 6 (dest) */
	.byte 0x78			/* ldl 8 (count) */
	.byte 0x25, 0xf2		/* sum */
	.byte 0x60, 0x8f		/* adc -1 */
	.byte 0xd1			/* stl 1 */

	/* src_end = src + count - 1 */
	.byte 0x77			/* ldl 7 (src) */
	.byte 0x78			/* ldl 8 (count) */
	.byte 0x25, 0xf2		/* sum */
	.byte 0x60, 0x8f		/* adc -1 */
	.byte 0xd2			/* stl 2 */

	/* remaining = count */
	.byte 0x78			/* ldl 8 */
	.byte 0xd3			/* stl 3 */

backward_loop:
	/* Check remaining count */
	.byte 0x73			/* ldl 3 (count) */
	.byte 0x21, 0xa5		/* cj 21 -> backward_done */

	/* Copy byte: load *src_end, store to *dest_end */
	.byte 0x71			/* ldl 1 (dest_end) */
	.byte 0x72			/* ldl 2 (src_end) -> AReg=src, BReg=dest */
	.byte 0xf1			/* lb -> AReg=byte, BReg=dest */
	.byte 0x20, 0xf0		/* rev -> AReg=dest, BReg=byte */
	.byte 0x23, 0xfb		/* sb -> store byte */

	/* src_end-- */
	.byte 0x72			/* ldl 2 */
	.byte 0x60, 0x8f		/* adc -1 */
	.byte 0xd2			/* stl 2 */

	/* dest_end-- */
	.byte 0x71			/* ldl 1 */
	.byte 0x60, 0x8f		/* adc -1 */
	.byte 0xd1			/* stl 1 */

	/* count-- */
	.byte 0x73			/* ldl 3 */
	.byte 0x60, 0x8f		/* adc -1 */
	.byte 0xd3			/* stl 3 */

	/* Jump back to backward_loop */
	.byte 0x61, 0x08		/* j -24 -> backward_loop */

backward_done:
	/* Skip past forward_move to epilogue */
	.byte 0x05			/* j 5 -> memmove_done */

forward_move:
	/* MOVE: copy count bytes from src to dest.
	 * Stack: AReg=count, BReg=dest, CReg=src. */
	.byte 0x77			/* ldl 7 (src -> will be CReg) */
	.byte 0x76			/* ldl 6 (dest -> will be BReg) */
	.byte 0x78			/* ldl 8 (count -> AReg) */
	.byte 0x24, 0xfa		/* pfix 4, opr A = MOVE (#4A) */

memmove_done:
	/* Return original dest */
	.byte 0x70			/* ldl 0 */
	.byte 0xb5			/* ajw 5 */
	.byte 0x22, 0xf0		/* ret */
