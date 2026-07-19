/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer optimized page operations using MOVE instruction
 *
 * copy_page_move(void *to, void *from)
 *   Copies PAGE_SIZE (4096) bytes using MOVE instruction.
 *   ~44x faster than byte-by-byte copy.
 *   MOVE at word speed: 2*(4096/4)+8 = 2056 cycles.
 *
 * clear_page_move(void *page)
 *   Clears PAGE_SIZE (4096) bytes to zero using MOVE doubling strategy:
 *   1. STNL to zero first 4 bytes
 *   2. MOVE 4 bytes (non-overlapping: [0..3] -> [4..7])
 *   3. MOVE 8 bytes ([0..7] -> [8..15])
 *   4. Continue doubling until 4096 bytes cleared
 *   Total: 12 MOVE calls, ~2200 cycles vs ~90000 for SB byte loop.
 *
 * TCWG calling convention after CALL:
 *   WPtr[+0]: Return IPtr
 *   WPtr[+4]: param0
 *   WPtr[+8]: param1
 */

.text

/* ================================================================
 * copy_page_move(void *to, void *from)
 *
 * Uses MOVE to copy 4096 bytes.
 * MOVE expects: AReg=count, BReg=dest, CReg=src.
 *
 * Byte map:
 * OFF  HEX    INSTRUCTION
 *  0   67     ldl 7 (src/from - param1, offset +4 from param0 +3 from retaddr)
 *
 * Wait - after CALL, retaddr is at WPtr[0], param0 at WPtr[1], param1 at WPtr[2].
 * But we allocate NO locals (no AJW needed for this simple function).
 * Actually we need to save return value... no, copy_page returns void.
 * So: param0 = WPtr[1] = ldl 1, param1 = WPtr[2] = ldl 2.
 *
 * But the TCWG convention says CALL pushes retaddr then decrements WPtr by 4 words,
 * placing retaddr at WPtr[0] and params at WPtr[1..3].
 *
 * WAIT: Looking at the actual calling convention used in this codebase:
 * After TCC's CALL mechanism:
 *   WPtr[0] = return address
 *   WPtr[1] = param0 (to)
 *   WPtr[2] = param1 (from)
 *
 * Actually, in transputer-gen.c, the CALL instruction saves retaddr at WPtr[0]
 * and params are pushed before. Looking at test_boot.S, params are at WPtr[1..3].
 * But in memcpy.S, after AJW -5, params are at WPtr[6..8] (offset 5 from retaddr).
 *
 * The convention seems to be that CALL saves retaddr then adjusts WPtr by -4,
 * so retaddr is at WPtr[0] and the 3 original param stack values are at
 * WPtr[4], WPtr[8], WPtr[12] (word offsets 1, 2, 3 ... no, +4, +8, +12).
 *
 * Looking more carefully at memcpy.S AJW -5:
 *   WPtr[5] = retaddr, WPtr[6] = param0, WPtr[7] = param1, WPtr[8] = param2
 * This means before AJW -5, the layout was:
 *   WPtr[0] = retaddr, WPtr[1] = param0, WPtr[2] = param1, WPtr[3] = param2
 * No, that's not right either. AJW -5 means WPtr -= 5 words.
 * Before AJW: retaddr at old_WPtr[0], params at [1],[2],[3].
 * After AJW -5: locals at [0..4], retaddr at [5], params at [6],[7],[8].
 *
 * So WITHOUT AJW (no locals needed):
 *   WPtr[0] = retaddr
 *   WPtr[1] = param0 (to)
 *   WPtr[2] = param1 (from)
 *
 * For copy_page, we don't need locals (void return, no state to save).
 *
 * OFF  HEX    INSTRUCTION
 *  0   72     ldl 2 (from/src -> will be CReg)
 *  1   71     ldl 1 (to/dest -> will be BReg)
 *  2   21 20 20 20 40   ldc 4096 = ldc 0x1000 (-> AReg)
 *  7   24 FA  MOVE (#4A)
 *  9   22 F0  ret
 *
 * ldc 4096 = 0x1000:
 *   PFIX 1 (0x21): OReg = 1 << 4 = 0x10
 *   PFIX 0 (0x20): OReg = 0x10 << 4 = 0x100
 *   PFIX 0 (0x20): OReg = 0x100 << 4 = 0x1000
 *   LDC 0 (0x40):  operand = 0x1000 | 0 = 0x1000 = 4096
 * ================================================================ */

.globl copy_page_move

copy_page_move:
	.byte 0x72			/* ldl 2 (from/src -> CReg) */
	.byte 0x71			/* ldl 1 (to/dest -> BReg) */
	.byte 0x21, 0x20, 0x20, 0x40	/* ldc 0x1000 = 4096 (-> AReg) */
	.byte 0x24, 0xfa		/* MOVE: opr 0x4A */
	.byte 0x22, 0xf0		/* ret */


/* ================================================================
 * clear_page_move(void *page)
 *
 * Clears 4096 bytes to zero using MOVE doubling strategy.
 *
 * Strategy:
 *   1. LDC 0; STNL 0 at page[0..3] (store word 0 at page base)
 *   2. MOVE 4 bytes from page[0..3] to page[4..7]
 *   3. MOVE 8 bytes from page[0..7] to page[8..15]
 *   4. MOVE 16 from [0..15] to [16..31]
 *   5. ... doubling 12 times total: 4,8,16,32,64,128,256,512,1024,2048
 *   After step 1: 4 bytes zeroed
 *   After MOVE 4: 8 bytes
 *   After MOVE 8: 16 bytes
 *   ...
 *   After MOVE 2048: 4096 bytes
 *   That's 10 MOVE calls (doubling from 4 to 2048).
 *
 * MOVE expects: CReg=src, BReg=dest, AReg=count.
 * For each doubling: src=page, dest=page+filled, count=filled.
 *
 * Workspace layout after AJW -2:
 *   WPtr[0]: local0 - filled count (doubles each iteration)
 *   WPtr[1]: local1 - page address
 *   WPtr[2]: return IPtr
 *   WPtr[3]: param0 (page)
 *
 * OFF  HEX    INSTRUCTION
 *  0   60 BE  ajw -2
 *  2   73     ldl 3 (page)
 *  3   D1     stl 1 (save page addr)
 *
 * Zero first word: STNL stores BReg at [AReg+offset*4]
 *  4   71     ldl 1 (page addr)
 *  5   40     ldc 0
 *  6   20 F0  rev -> AReg=page, BReg=0
 *  8   E0     stnl 0 -> Mem[page] = 0
 *
 * Set filled = 4
 *  9   44     ldc 4
 * 10   D0     stl 0
 *
 * Loop: while filled < 4096, MOVE filled bytes then double
 * loop:
 * 11   71     ldl 1 (page = src -> CReg)
 * 12   71     ldl 1 (page -> will compute dest)
 * 13   70     ldl 0 (filled)
 * 14   25 F2  sum   -> page + filled (= dest -> BReg after next push)
 *
 * Hmm, stack management is tricky. After ldl 1, ldl 1, ldl 0, sum:
 *   CReg = page (from first ldl 1)
 *   BReg = page + filled (from sum of second ldl 1 and filled)
 *   AReg = undefined (sum consumes AReg and BReg, leaves result in AReg)
 *
 * Wait: after ldl 1 (page): AReg=page
 *       after ldl 1 (page): AReg=page, BReg=page
 *       after ldl 0 (filled): AReg=filled, BReg=page, CReg=page
 *       after sum: AReg=page+filled, BReg=page
 *       But we need: AReg=count, BReg=dest, CReg=src
 *       Currently: AReg=dest(page+filled), BReg=src(page)
 *       Need to add count. Push filled:
 *       ldl 0 (filled): AReg=filled, BReg=dest, CReg=src. PERFECT!
 *
 * So: ldl 1, ldl 1, ldl 0, sum, ldl 0, MOVE
 *
 * 11   71     ldl 1 (page)
 * 12   71     ldl 1 (page)
 * 13   70     ldl 0 (filled)
 * 14   25 F2  sum -> AReg=page+filled
 * 16   70     ldl 0 (filled -> AReg=filled, BReg=page+filled, CReg=page)
 * 17   24 FA  MOVE
 *
 * Double filled:
 * 19   70     ldl 0 (filled)
 * 20   70     ldl 0 (filled -> AReg=filled, BReg=filled)
 * 21   25 F2  sum -> AReg = filled*2
 * 23   D0     stl 0 (filled = filled*2)
 *
 * Check if filled < 4096:
 * 24   21 20 20 40  ldc 4096
 * 28   70     ldl 0 (filled -> AReg=filled, BReg=4096)
 * 29   F9     gt -> (4096 > filled) ? 1 : 0
 *
 * Wait: GT tests BReg > AReg. After ldc 4096, ldl 0:
 *   AReg=filled, BReg=4096. GT -> (4096 > filled). YES, this is filled < 4096.
 *
 * If filled < 4096 (AReg=1), we want to loop back.
 * CJ: jump if AReg==0 (filled >= 4096 -> done).
 * CJ jumps to done. If AReg=1, fall through to... but we need to LOOP.
 * We want: if filled < 4096, jump BACK. CJ jumps FORWARD.
 *
 * Alternative: check filled >= 4096 and CJ to skip the loop body.
 * Or: unconditionally MOVE, then check and jump back.
 *
 * Better: check at TOP of loop. If filled >= 4096, CJ forward to done.
 * If filled < 4096, fall through to MOVE body, then J back.
 *
 * Revised loop:
 * loop_start:
 * 11   21 20 20 40  ldc 4096
 * 15   70     ldl 0 (filled)
 * 16   F9     gt -> (4096 > filled) = (filled < 4096)
 * 17   A?     cj -> done if filled >= 4096
 *
 * MOVE body:
 * 18   71     ldl 1 (page -> CReg)
 * 19   71     ldl 1 (page)
 * 20   70     ldl 0 (filled)
 * 21   25 F2  sum -> page+filled (dest -> BReg)
 * 23   70     ldl 0 (filled -> count, AReg)
 * 24   24 FA  MOVE
 *
 * Double:
 * 26   70     ldl 0
 * 27   70     ldl 0
 * 28   25 F2  sum -> filled*2
 * 30   D0     stl 0
 *
 * J back:
 * 31   ??     j -> loop_start(11)
 *
 * done:
 * 33   B2     ajw 2
 * 34   22 F0  ret
 *
 * CJ at 17: after CJ, IPtr=18. Target=done(33). Offset=33-18=15. CJ F=0xAF. 1 byte!
 * J at 31: after J, IPtr=33. Target=11. Offset=11-33=-22.
 *   -22: 0xFFFFFFEA. Low nibble=A. ~(-22)=21. 21>>4=1.
 *   NFIX 1 (0x61), J A (0x0A). 2 bytes.
 * So J at offsets 31-32. done at 33. CJ offset = 33-18 = 15. 0xAF. Correct!
 *
 * Total: 36 bytes. Very compact!
 * ================================================================ */

.globl clear_page_move

clear_page_move:
	/* Allocate 2 locals */
	.byte 0x60, 0xbe		/* ajw -2 */

	/* Save page address */
	.byte 0x73			/* ldl 3 (param0 = page) */
	.byte 0xd1			/* stl 1 */

	/* Zero first word: STNL stores BReg at [AReg] */
	.byte 0x71			/* ldl 1 (page) */
	.byte 0x40			/* ldc 0 */
	.byte 0x20, 0xf0		/* rev -> AReg=page, BReg=0 */
	.byte 0xe0			/* stnl 0 -> Mem[page] = 0 */

	/* filled = 4 */
	.byte 0x44			/* ldc 4 */
	.byte 0xd0			/* stl 0 */

clear_loop:
	/* Check: filled < 4096 ? */
	.byte 0x21, 0x20, 0x20, 0x40	/* ldc 4096 (0x1000) */
	.byte 0x70			/* ldl 0 (filled) -> AReg=filled, BReg=4096 */
	.byte 0xf9			/* gt -> (4096 > filled) ? 1 : 0 */
	.byte 0xaf			/* cj 15 -> clear_done if filled >= 4096 */

	/* MOVE: copy 'filled' bytes from page to page+filled */
	.byte 0x71			/* ldl 1 (page = src -> CReg) */
	.byte 0x71			/* ldl 1 (page) */
	.byte 0x70			/* ldl 0 (filled) */
	.byte 0x25, 0xf2		/* sum -> page+filled (dest -> BReg) */
	.byte 0x70			/* ldl 0 (filled = count -> AReg) */
	.byte 0x24, 0xfa		/* MOVE: opr 0x4A */

	/* filled *= 2 */
	.byte 0x70			/* ldl 0 (filled) */
	.byte 0x70			/* ldl 0 (filled) */
	.byte 0x25, 0xf2		/* sum -> filled*2 */
	.byte 0xd0			/* stl 0 */

	/* Jump back to clear_loop */
	.byte 0x61, 0x0a		/* j -22 -> clear_loop */

clear_done:
	/* Epilogue */
	.byte 0xb2			/* ajw 2 */
	.byte 0x22, 0xf0		/* ret */
