/*
 * thds_selth.S — T9000 selth / ldth + L-trap entry stub.
 *
 * SPINE RTL CALL (t9_exec / t9_core):
 *   After CALL: A = return Iptr (also stored @ W[0]), B = pre-CALL A (arg0),
 *   C = pre-CALL B (arg1).  Full T4 4-word ABC memory frame is also written
 *   by call_active when enabled; do not rely only on ldl 1 until that path
 *   is proven on SDRAM fabric.
 *
 * App B: selth 0x60F9, ldth 0x2CF2, tret 0x60FB, ret 0x20F0
 */

.section .text,"ax"
.align 4

.global t9k_asm_selth
t9k_asm_selth:
	/* Entry: A=return, B=arg0 (THDS).  Need A=THDS for selth. */
	.byte 0xF0			/* rev → A=arg0, B=return */
	/* secondary 0x60F9 */
	.byte 0x26, 0x20, 0x2F, 0xF9
	.byte 0x22, 0xF0		/* ret (link still @ W[0]) */

.global t9k_asm_ldth
t9k_asm_ldth:
	/* secondary 0x2CF2 */
	.byte 0x22, 0x2C, 0x2F, 0xF2
	.byte 0x22, 0xF0		/* ret — A = ThReg */

.global t9k_ltrap_entry
t9k_ltrap_entry:
	.byte 0x25, 0x44		/* ldc 'T' */
	.byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
	.byte 0xE0
	.byte 0x40			/* ldc 0 */
	.byte 0x26, 0x20, 0x2F, 0xFB	/* tret restore */
	.byte 0x25, 0x55		/* 'U' fallback */
	.byte 0x21, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x40
	.byte 0xE0
	.byte 0x21, 0xF5		/* stopp */
