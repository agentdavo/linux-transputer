/*
 * thds_selth.S — TP32X selth / ldth + L-trap entry stub.
 *
 * Architectural CALL stores the 4-word frame (return IPtr at W[0], arg0 at
 * W[1], arg1 at W[2], arg2 at W[3]) on both RTL and the software emulator.
 *
 * Byte sequences:
 *   selth = 60 F9  -> nfix 0 ; opr 9   -> OReg = -7  (negop(0,9))
 *   ldth  = 2C F2  -> pfix 12; opr 2   -> OReg = 0xC2
 *   tret  = 60 FB  -> nfix 0 ; opr 11  -> OReg = -5  (negop(0,0xB))
 *   ret   = 22 F0  -> pfix 2 ; opr 0   -> OReg = 0x20 (ret)
 */

.section .text,"ax"
.align 4

.global tp_asm_selth
tp_asm_selth:
	.byte 0x71                      /* ldl 1: A = arg0 */
	.byte 0x60, 0xF9		/* selth: nfix 0; opr 9  (OReg = -7) */
	.byte 0x22, 0xF0		/* ret (link still @ W[0]) */

.global tp_asm_ldth
tp_asm_ldth:
	.byte 0x2C, 0xF2		/* ldth: pfix 12; opr 2  (OReg = 0xC2) */
	.byte 0x22, 0xF0		/* ret — A = ThReg */

.global tp_ltrap_entry
tp_ltrap_entry:
	.byte 0x25, 0x44		/* ldc 'T' */
	.byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
	.byte 0xE0
	.byte 0x40			/* ldc 0 */
	.byte 0x60, 0xFB		/* tret: nfix 0; opr 11 (OReg = -5) */
	.byte 0x25, 0x55		/* 'U' fallback */
	.byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
	.byte 0xE0
	.byte 0x21, 0xF5		/* stopp */
