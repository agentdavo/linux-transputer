/* SPDX-License-Identifier: GPL-2.0 */
/* Experimental supervisor aligned user-word load. A = VA, B = physical root,
 * C = supervisor exception vector. Wptr remains physical throughout. */
.section .text,"ax"
.global transputer_paged_read
transputer_paged_read:
	ldl 3
	ldl 2
	ldl 1
.global transputer_paged_read_pc
transputer_paged_read_pc:
	.byte 0x22, 0x2f, 0xfd
	ret

.global transputer_page_vector
transputer_page_vector:
	call transputer_boot_page_fault
	.byte 0x22, 0x2f, 0xfc
