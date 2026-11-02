/*
 * thds_selth.S — T9000 selth / ldth + L-trap entry stub.
 *
 * SPINE RTL CALL (t9_exec / t9_core):
 *   After CALL: A = return Iptr (also stored @ W[0]), B = pre-CALL A (arg0),
 *   C = pre-CALL B (arg1).  Full T4 4-word ABC memory frame is also written
 *   by call_active when enabled; do not rely only on ldl 1 until that path
 *   is proven on SDRAM fabric.
 *
 * App B lists these as MEMORY BYTE SEQUENCES, not as operand values:
 *   selth = 60 F9  -> nfix 0 ; opr 9   -> OReg = -7  (negop(0,9))
 *   ldth  = 2C F2  -> pfix 12; opr 2   -> OReg = 0xC2
 *   tret  = 60 FB  -> nfix 0 ; opr 11  -> OReg = -5  (negop(0,0xB))
 *   ret   = 20 F0  -> pfix 0 ; opr 0   -> OReg = 0x20 (ret)
 *
 * These were previously emitted as pfix chains that BUILT the hex string as
 * a value (26 20 2F F9 -> OReg = 0x60F9), which is a different, undefined
 * secondary.  The T4 emulator was taught to accept both spellings so an
 * un-rebuilt kernel still boots, but the ISM encoding is the correct one and
 * the only one the RTL is obliged to implement.
 */

.section .text,"ax"
.align 4

.global t9k_asm_selth
t9k_asm_selth:
	/* Entry: A=return, B=arg0 (THDS).  Need A=THDS for selth. */
	.byte 0xF0			/* rev → A=arg0, B=return */
	.byte 0x60, 0xF9		/* selth: nfix 0; opr 9  (OReg = -7) */
	.byte 0x22, 0xF0		/* ret (link still @ W[0]) */

.global t9k_asm_ldth
t9k_asm_ldth:
	.byte 0x2C, 0xF2		/* ldth: pfix 12; opr 2  (OReg = 0xC2) */
	.byte 0x22, 0xF0		/* ret — A = ThReg */

.global t9k_ltrap_entry
t9k_ltrap_entry:
	.byte 0x25, 0x44		/* ldc 'T' */
	.byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
	.byte 0xE0
	.byte 0x40			/* ldc 0 */
	.byte 0x60, 0xFB		/* tret: nfix 0; opr 11 (OReg = -5) */
	.byte 0x25, 0x55		/* 'U' fallback */
	.byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
	.byte 0xE0
	.byte 0x21, 0xF5		/* stopp */
