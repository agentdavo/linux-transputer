/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_UACCESS_H
#define _ASM_TRANSPUTER_UACCESS_H

struct pt_regs;
extern int fixup_exception(struct pt_regs *regs);

/*
 * Transputer uaccess — NOMMU, flat memory.
 * Delegate entirely to asm-generic/uaccess.h which provides
 * copy_from_user, copy_to_user, get_user, put_user, etc.
 */

/*
 * Fix strnlen_user: the kernel expects strnlen_user() to return the
 * length INCLUDING the NUL terminator (or n if no NUL found within n).
 * The asm-generic default maps __strnlen_user to strnlen which does NOT
 * count the NUL.  Override it before including the generic header.
 */
static inline long __transputer_strnlen_user(const char *s, long n)
{
	long len = strnlen(s, n);
	if (len < n)
		len++; /* count the NUL terminator */
	return len;
}
#define __strnlen_user __transputer_strnlen_user

#include <asm-generic/uaccess.h>

#endif /* _ASM_TRANSPUTER_UACCESS_H */
