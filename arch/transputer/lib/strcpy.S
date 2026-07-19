/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer assembly strcpy implementation
 *
 * Separated into its own file to work around a TCC assembler bug where
 * the first .globl/label in a .S file gets silently dropped.
 *
 * char *strcpy(char *dest, const char *src)
 *
 * TCWG calling convention after CALL:
 *   WPtr[+0]: Return IPtr
 *   WPtr[+4]: param0 (dest)
 *   WPtr[+8]: param1 (src)
 *
 * Workspace layout after AJW -4:
 *   WPtr[0]: local0 - original dest (return value)
 *   WPtr[1]: local1 - current dest
 *   WPtr[2]: local2 - current src
 *   WPtr[3]: local3 - current byte
 *   WPtr[4]: return IPtr
 *   WPtr[5]: param0 (dest)
 *   WPtr[6]: param1 (src)
 */

.text

.globl strcpy
strcpy:
	/* AJW -4 */
	.byte 0x60			/* nfix 0 */
	.byte 0xbc			/* ajw 12 -> AJW -4 */

	/* Save original dest and initialize cursors */
	.byte 0x75			/* ldl 5 -> dest */
	.byte 0xd0			/* stl 0 */
	.byte 0x75			/* ldl 5 -> dest */
	.byte 0xd1			/* stl 1 */
	.byte 0x76			/* ldl 6 -> src */
	.byte 0xd2			/* stl 2 */

strcpy_loop:
	/* ch = *src */
	.byte 0x72			/* ldl 2 */
	.byte 0xf1			/* lb */
	.byte 0xd3			/* stl 3 */

	/* *dest = ch */
	.byte 0x71			/* ldl 1 -> dest */
	.byte 0x73			/* ldl 3 -> ch (AReg), dest (BReg) */
	.byte 0x23, 0xfb		/* sb */

	/* if (ch == 0) goto done */
	.byte 0x73			/* ldl 3 */
	.byte 0xa8			/* cj 8 */

	/* dest++ */
	.byte 0x71			/* ldl 1 */
	.byte 0x81			/* adc 1 */
	.byte 0xd1			/* stl 1 */

	/* src++ */
	.byte 0x72			/* ldl 2 */
	.byte 0x81			/* adc 1 */
	.byte 0xd2			/* stl 2 */

	/* jump back to loop: j -17 */
	.byte 0x61, 0x0f		/* nfix 1, j F */

strcpy_done:
	/* return original dest */
	.byte 0x70			/* ldl 0 */
	.byte 0xb4			/* ajw +4 */
	.byte 0x22, 0xf0		/* ret */
