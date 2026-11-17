/* ISM 13.2.3: goprot switches the integer P-state; the supervisor
 * explicitly switches floating-point and block-move registers.
 */
.section .text,"ax"

#define FPSTALL      .byte 0x60, 0xff
#define FPLDALL      .byte 0x60, 0xfe
#define STMOVE2DINIT .byte 0x61, 0xf0

/* Make an initial FP image in the processor's own format. Do this only
 * for a fresh P entry. Preserve the supervisor's flags and FP registers.
 */
.global t9k_asm_init_fp
t9k_asm_init_fp:
	ajw -8
	ldflags
	stl 7
	ldlp 0
	FPSTALL
	ldc 0
	stflags
	fpldzerosn
	fpldzerosn
	fpldzerosn
	fprn
	ldl 9			/* caller's W[1]: new FP image */
	FPSTALL
	ldl 7
	stflags
	ldlp 0
	FPLDALL
	ajw 8
	ret

/* W[1..3] = PDS, RODS, extra. The first seven words of extra are the
 * FP save image, followed by block count, destination and source strides.
 */
.global t9k_asm_goprot
t9k_asm_goprot:
	ajw -12			/* L FP[0..6], block[7..9], reason, type */
	ldlp 0
	FPSTALL
	ldlp 7
	STMOVE2DINIT

	ldl 15
	ldnl 9			/* source stride */
	ldl 15
	ldnl 8			/* destination stride */
	ldl 15
	ldnl 7			/* row count */
	move2dinit
	ldl 15
	FPLDALL
	ldl 14			/* RODS */
	ldl 13			/* PDS */
	.byte 0x60, 0xfa	/* goprot */

	/* Stash the ISM return pair before either state operation uses A.
	 * Capture P immediately, before any supervisor FP or block move.
	 */
	stl 10
	stl 11
	ldl 15
	FPSTALL
	ldl 15
	ldnlp 7
	STMOVE2DINIT

	ldl 9
	ldl 8
	ldl 7
	move2dinit
	ldlp 0
	FPLDALL
	ldl 11
	ldc 8
	shl
	ldl 10
	or			/* (et.type << 8) | t.reason */
	ajw 12
	ret

.global t9k_asm_syscall
t9k_asm_syscall:
	.byte 0x60, 0xF8	/* syscall */
	.byte 0x22, 0xF0	/* ret */
