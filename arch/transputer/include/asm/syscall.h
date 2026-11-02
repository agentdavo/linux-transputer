/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SYSCALL_H
#define _ASM_TRANSPUTER_SYSCALL_H

/* Transputer doesn't use traditional syscalls - placeholder for now */

#include <linux/audit.h>
#include <linux/compat.h>

/* Syscall audit architecture - for now use a placeholder value */
static inline int syscall_get_arch(struct task_struct *task)
{
	/* TODO: Define proper AUDIT_ARCH_TRANSPUTER constant in audit.h */
	return 0; /* Placeholder - audit not implemented for transputer yet */
}

#endif /* _ASM_TRANSPUTER_SYSCALL_H */
