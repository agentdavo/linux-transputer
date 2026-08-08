/*
 * t9000_goprot.S — Enter T9000 protected mode / emit syscall (Phase 3–4).
 *
 * void t9k_asm_goprot(unsigned long pds_phys, unsigned long rods_phys);
 * void t9k_asm_syscall(void);
 *
 * T4/Inmos CALL frame (see t4/p.c case 0x90):
 *   writeword(WPtr-1, C); writeword(WPtr-2, B); writeword(WPtr-3, A);
 *   writeword(WPtr-4, IPtr);  WPtr := WPtr-4;  A := return;
 * So at callee entry (no ajw):
 *   WPtr[0] = return IPtr
 *   WPtr[1] = pre-CALL A  = arg0 (pds)   when TCC pushed A=arg0,B=arg1
 *   WPtr[2] = pre-CALL B  = arg1 (rods)
 *   WPtr[3] = pre-CALL C
 *
 * goprot needs A=PDS, B=RODS (bytes 60 FA).
 * RET reads IPtr from WPtr[0] and does WPtr += 4.
 */
.section .text,"ax"

.global t9k_asm_goprot
t9k_asm_goprot:
	.byte 0x71              /* ldl 1 — A = pds (CALL-saved A) */
	.byte 0x72              /* ldl 2 — A = rods, B = pds */
	.byte 0xF0              /* rev  — A = pds, B = rods */
	.byte 0x60, 0xFA        /* goprot */
	/* WPtr[0] still holds return address */
	.byte 0x22, 0xF0        /* ret */

.global t9k_asm_syscall
t9k_asm_syscall:
	/* No args; CALL frame still has return at [0] */
	.byte 0x60, 0xF8        /* syscall */
	.byte 0x22, 0xF0        /* ret */
