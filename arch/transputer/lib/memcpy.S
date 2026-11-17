/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer assembly memcpy implementation using MOVE instruction
 *
 * void *memcpy(void *dest, const void *src, size_t count)
 *
 * Uses the T800 MOVE instruction (#4A) which copies Areg bytes from
 * address Creg to address Breg.  This is ~44x faster than a byte-by-byte
 * SB loop for large copies (2w+8 cycles vs ~22 instructions per byte).
 *
 * MOVE is interruptable by high-priority processes, so it is safe to
 * use for arbitrarily large copies.
 *
 * Precondition: source and destination must NOT overlap.
 *               (Use memmove for potentially overlapping regions.)
 *
 * TCWG calling convention after CALL:
 *   WPtr[+0]: Return IPtr
 *   WPtr[+4]: param0 (dest)  - destination pointer
 *   WPtr[+8]: param1 (src)   - source pointer
 *   WPtr[+12]: param2 (count) - number of bytes
 *
 * Returns: dest (original destination pointer)
 *
 * Workspace layout after AJW -1:
 *   WPtr[0]: local0 - saved original dest (return value)
 *   WPtr[4]: return IPtr
 *   WPtr[8]: param0 (dest)
 *   WPtr[12]: param1 (src)
 *   WPtr[16]: param2 (count)
 *
 * MOVE instruction expects:
 *   Areg = count (number of bytes)
 *   Breg = dest  (destination address)
 *   Creg = src   (source address)
 *
 * Load order (last loaded = Areg):
 *   load src   -> Areg=src
 *   load dest  -> Areg=dest, Breg=src
 *   load count -> Areg=count, Breg=dest, Creg=src
 *   MOVE
 */

.text
.globl memcpy

memcpy:
	/* Allocate 1 local word: ajw -1 */
	/* -1 = 0xFFFFFFFF, so: NFIX 0 (0x60), AJW F (0xBF) */
	.byte 0x60			/* nfix 0 */
	.byte 0xbf			/* ajw 15 -> ajw -1 */

	/* Save original dest to local0 (return value) */
	.byte 0x72			/* ldl 2 -> param0 (dest) */
	.byte 0xd0			/* stl 0 */

	/* Check count == 0, skip MOVE if so */
	/* MOVE is undefined for negative count; 0 is safe but wasteful */
	.byte 0x74			/* ldl 4 -> param2 (count) */
	.byte 0xa5			/* cj +5 -> jump to memcpy_done if count==0 */

	/* Load eval stack for MOVE: need Creg=src, Breg=dest, Areg=count */
	.byte 0x73			/* ldl 3 -> src (will become Creg) */
	.byte 0x72			/* ldl 2 -> dest (pushes src to Breg) */
	.byte 0x74			/* ldl 4 -> count (pushes dest to Breg, src to Creg) */

	/* Wait - after these 3 loads: Areg=count, Breg=dest, Creg=src. Perfect. */

	/* MOVE: copy Areg bytes from Creg(src) to Breg(dest) */
	.byte 0x24, 0xfa		/* pfix 4, opr A = opr 0x4A = move */

memcpy_done:
	/* Load return value */
	.byte 0x70			/* ldl 0 -> saved dest */

	/* Deallocate frame: ajw +1 */
	.byte 0xb1			/* ajw 1 */

	/* Return */
	.byte 0x22, 0xf0		/* ret */
