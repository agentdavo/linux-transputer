/*
 * t9000_goprot.S — Enter T9000 protected mode / emit syscall (Phase 3–4).
 *
 * void t9k_asm_goprot(unsigned long pds_phys, unsigned long rods_phys);
 * void t9k_asm_syscall(void);
 *
 * SPINE CALL (after exec): A=return Iptr (@W[0]), B=arg0, C=arg1.
 * T4 also writes ABC into W[1..3]; SPINE call_active does the same when
 * multi-beat frame is healthy.  Prefer eval-stack reshuffle so either path works.
 *
 * goprot needs A=PDS, B=RODS (bytes 60 FA).
 * RET reads IPtr from WPtr[0] and does WPtr += 4.
 */
.section .text,"ax"

.global t9k_asm_goprot
t9k_asm_goprot:
	/* Entry: A=ret, B=pds, C=rods → need A=pds, B=rods */
	.byte 0xD1		/* stl 1: drop ret from A → A=pds, B=rods */
	.byte 0x60, 0xFA	/* goprot */
	.byte 0x22, 0xF0	/* ret */

.global t9k_asm_syscall
t9k_asm_syscall:
	.byte 0x60, 0xF8	/* syscall */
	.byte 0x22, 0xF0	/* ret */
