/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer assembly strcmp and strlen implementations
 *
 * These avoid TCC backward jump miscompilation bugs that cause
 * infinite loops when the C versions are compiled.
 *
 * NOTE: strcpy is in its own file (strcpy.S) because the TCC assembler
 * silently drops the first .globl/label pair in a .S file.
 *
 * Opcode reference (from dastabs.h):
 *   LB   = OPR 0x01 = 0xF1 (load byte)
 *   DIFF = OPR 0x04 = 0xF4 (unsigned subtract)
 *   SB   = OPR 0x3B = 0x23 0xFB (store byte)
 *   REV  = OPR 0x00 = 0x20 0xF0 (reverse top two stack)
 *   RET  = OPR 0x20 = 0x22 0xF0 (return)
 */

.text

/*
 * int strcmp(const char *cs, const char *ct)
 *
 * TCWG calling convention after CALL:
 *   WPtr[+0]: Return IPtr
 *   WPtr[+4]: param0 (cs)  - first string pointer
 *   WPtr[+8]: param1 (ct)  - second string pointer
 *
 * Returns: 0 if equal, <0 if cs<ct, >0 if cs>ct
 *
 * Workspace layout after AJW -4:
 *   WPtr[0]: local0 - cs ptr (current)
 *   WPtr[1]: local1 - ct ptr (current)
 *   WPtr[2]: local2 - byte from cs
 *   WPtr[3]: local3 - byte from ct
 *   WPtr[4]: return IPtr
 *   WPtr[5]: param0 (cs)
 *   WPtr[6]: param1 (ct)
 */
.globl strcmp
strcmp:
	/* AJW -4: allocate 4 locals */
	.byte 0x60			/* nfix 0 */
	.byte 0xbc			/* ajw 12  -> AJW -4 */

	/* Copy params to locals */
	.byte 0x75			/* ldl 5 -> AReg = cs */
	.byte 0xd0			/* stl 0 -> local0 = cs */
	.byte 0x76			/* ldl 6 -> AReg = ct */
	.byte 0xd1			/* stl 1 -> local1 = ct */

	/*
	 * Loop byte map (relative to strcmp_loop):
	 * Offset  0: ldl 0      (70)           1 byte
	 * Offset  1: lb         (f1)           1 byte  - load byte [cs]
	 * Offset  2: stl 2      (d2)           1 byte
	 * Offset  3: ldl 1      (71)           1 byte
	 * Offset  4: lb         (f1)           1 byte  - load byte [ct]
	 * Offset  5: stl 3      (d3)           1 byte
	 * Offset  6: ldl 2      (72)           1 byte
	 * Offset  7: ldl 3      (73)           1 byte
	 * Offset  8: diff       (f4)           1 byte  - cs_byte - ct_byte
	 * Offset  9: cj 1       (a1)           1 byte  - if diff==0, skip to NUL check
	 * Offset 10: j 10       (0a)           1 byte  - diff!=0, jump to exit (offset 21)
	 * Offset 11: ldl 2      (72)           1 byte  - NUL check
	 * Offset 12: cj 8       (a8)           1 byte  - if cs_byte==0, exit (offset 21)
	 * Offset 13: ldl 0      (70)           1 byte
	 * Offset 14: adc 1      (81)           1 byte  - cs++
	 * Offset 15: stl 0      (d0)           1 byte
	 * Offset 16: ldl 1      (71)           1 byte
	 * Offset 17: adc 1      (81)           1 byte  - ct++
	 * Offset 18: stl 1      (d1)           1 byte
	 * Offset 19: j -21      (61 0b)        2 bytes - back to loop start
	 * Offset 21: (strcmp_exit)
	 *
	 * CJ 1 at offset 9:  IPtr=10, target=10+1=11  (NUL check)
	 * J 10 at offset 10:  IPtr=11, target=11+10=21 (exit)
	 * CJ 8 at offset 12: IPtr=13, target=13+8=21  (exit)
	 * J -21 at offset 19: NFIX 1 (0x61), J B (0x0B)
	 *   OReg = ~1<<4 = 0xFFFFFFE0, offset = 0xFFFFFFE0|B = -21
	 *   IPtr=21, target=21+(-21)=0
	 */

strcmp_loop:
	/* Load byte from cs */
	.byte 0x70			/* ldl 0  -> AReg = cs ptr */
	.byte 0xf1			/* lb     -> AReg = byte at [AReg] */
	.byte 0xd2			/* stl 2  -> local2 = cs byte */

	/* Load byte from ct */
	.byte 0x71			/* ldl 1  -> AReg = ct ptr */
	.byte 0xf1			/* lb     -> AReg = byte at [AReg] */
	.byte 0xd3			/* stl 3  -> local3 = ct byte */

	/* Compare: diff = cs_byte - ct_byte */
	.byte 0x72			/* ldl 2  -> AReg = cs byte */
	.byte 0x73			/* ldl 3  -> AReg = ct byte, BReg = cs byte */
	.byte 0xf4			/* diff   -> AReg = BReg - AReg */

	/* If diff != 0, exit with diff as return value */
	.byte 0xa1			/* cj 1: if AReg==0, skip to NUL check */
	.byte 0x0a			/* j 10: diff!=0, jump to strcmp_exit */

	/* Bytes equal - check for NUL terminator */
	.byte 0x72			/* ldl 2  -> AReg = cs byte */
	.byte 0xa8			/* cj 8: if cs_byte==0, exit (return 0) */

	/* Advance pointers */
	.byte 0x70			/* ldl 0  -> cs ptr */
	.byte 0x81			/* adc 1 */
	.byte 0xd0			/* stl 0 */
	.byte 0x71			/* ldl 1  -> ct ptr */
	.byte 0x81			/* adc 1 */
	.byte 0xd1			/* stl 1 */

	/* Jump back to loop start: j -21 */
	.byte 0x61, 0x0b		/* nfix 1, j B */

strcmp_exit:
	/* AReg has diff (cs_byte - ct_byte) or 0 */
	/* Deallocate frame: ajw +4 */
	.byte 0xb4			/* ajw 4 */
	.byte 0x22, 0xf0		/* ret */




/*
 * __kernel_size_t strlen(const char *s)
 *
 * TCWG calling convention after CALL:
 *   WPtr[+0]: Return IPtr
 *   WPtr[+4]: param0 (s)  - string pointer
 *
 * Returns: length of string (not including NUL)
 *
 * Workspace layout after AJW -3:
 *   WPtr[0]: local0 - current ptr
 *   WPtr[1]: local1 - original ptr (for subtraction at end)
 *   WPtr[2]: local2 - current byte
 *   WPtr[3]: return IPtr
 *   WPtr[4]: param0 (s)
 */
.globl strlen
strlen:
	/* AJW -3: allocate 3 locals */
	/* -3: NFIX 0 (0x60), AJW D (0xBD) */
	.byte 0x60			/* nfix 0 */
	.byte 0xbd			/* ajw 13 -> AJW -3 */

	/* Copy s to local0 (current) and local1 (original) */
	.byte 0x74			/* ldl 4 -> AReg = s */
	.byte 0xd0			/* stl 0 */
	.byte 0x74			/* ldl 4 -> AReg = s */
	.byte 0xd1			/* stl 1 */

	/*
	 * Loop byte map (relative to strlen_loop):
	 * Offset 0: ldl 0      (70)           1 byte
	 * Offset 1: lb         (f1)           1 byte  - load byte
	 * Offset 2: cj 5       (a5)           1 byte  - if byte==0, done
	 * Offset 3: ldl 0      (70)           1 byte
	 * Offset 4: adc 1      (81)           1 byte
	 * Offset 5: stl 0      (d0)           1 byte
	 * Offset 6: j -8       (60 08)        2 bytes - back to start
	 * Offset 8: (strlen_done)
	 *
	 * CJ 5 at offset 2: IPtr=3, target=3+5=8 (done)
	 * J -8 at offset 6: NFIX 0 (0x60), J 8 (0x08)
	 *   OReg = ~0<<4 = 0xFFFFFFF0, offset = 0xFFFFFFF0|8 = -8
	 *   IPtr=8, target=8+(-8)=0
	 */
strlen_loop:
	.byte 0x70			/* ldl 0  -> AReg = current ptr */
	.byte 0xf1			/* lb     -> AReg = byte at [AReg] */
	.byte 0xa5			/* cj 5:  if byte==0, jump to strlen_done */

	.byte 0x70			/* ldl 0  -> current ptr */
	.byte 0x81			/* adc 1 */
	.byte 0xd0			/* stl 0 */

	/* Jump back to loop */
	.byte 0x60, 0x08		/* nfix 0, j 8 -> j -8 */

strlen_done:
	/* Return current - original */
	.byte 0x70			/* ldl 0  -> AReg = current ptr */
	.byte 0x71			/* ldl 1  -> AReg = original, BReg = current */
	.byte 0xf4			/* diff   -> AReg = BReg - AReg = length */

	/* Deallocate frame: ajw +3 */
	.byte 0xb3			/* ajw 3 */
	.byte 0x22, 0xf0		/* ret */
