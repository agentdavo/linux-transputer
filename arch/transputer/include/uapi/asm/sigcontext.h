/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_TRANSPUTER_SIGCONTEXT_H
#define _ASM_TRANSPUTER_SIGCONTEXT_H

struct sigcontext {
	unsigned long wptr;      /* Workspace pointer */
	unsigned long iptr;      /* Instruction pointer */
	unsigned long areg;      /* A register */
	unsigned long breg;      /* B register */
	unsigned long creg;      /* C register */
	unsigned long status;    /* Status register */
};

#endif /* _ASM_TRANSPUTER_SIGCONTEXT_H */
