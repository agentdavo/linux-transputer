/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_TRANSPUTER_PTRACE_H
#define _UAPI_ASM_TRANSPUTER_PTRACE_H

/* Transputer register offsets for ptrace */
#define PT_WPTR    0
#define PT_IPTR    1
#define PT_AREG    2
#define PT_BREG    3
#define PT_CREG    4
#define PT_STATUS  5

/* user_regs_struct for Transputer */
struct user_regs_struct {
	unsigned long wptr;
	unsigned long iptr;
	unsigned long areg;
	unsigned long breg;
	unsigned long creg;
	unsigned long status;
};

#endif /* _UAPI_ASM_TRANSPUTER_PTRACE_H */
