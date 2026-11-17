/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_TRANSPUTER_UNISTD_H
#define _UAPI_ASM_TRANSPUTER_UNISTD_H

#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_TIME32_SYSCALLS
#define __ARCH_WANT_RENAMEAT

/* Transputer uses Linux's generic 32-bit syscall numbering.  The trap
 * mechanism is architecture-specific, but the userspace ABI numbers are not. */
#include <asm-generic/unistd.h>

#endif /* _UAPI_ASM_TRANSPUTER_UNISTD_H */
