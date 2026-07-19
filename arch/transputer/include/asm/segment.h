/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SEGMENT_H
#define _ASM_TRANSPUTER_SEGMENT_H

/*
 * Transputer segment definitions for asm-generic/uaccess.h.
 *
 * On this NOMMU port, USER_DS and KERNEL_DS are address limits, not
 * hardware segment selectors. Kernel-internal syscalls depend on set_fs()
 * switching addr_limit to KERNEL_DS before passing kernel pointers.
 */

#ifndef __ASSEMBLY__

#include <asm/processor.h>
#include <linux/thread_info.h>

typedef struct {
	unsigned long seg;
} mm_segment_t;

/* Stop asm-generic/uaccess.h from redefining get_fs/set_fs/get_ds. */
#define get_fs get_fs

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })
#define USER_DS		MAKE_MM_SEG(TASK_SIZE - 1)
#define KERNEL_DS	MAKE_MM_SEG(~0UL)

static inline mm_segment_t get_fs(void)
{
	return MAKE_MM_SEG(current_thread_info()->addr_limit);
}

static inline mm_segment_t get_ds(void)
{
	return KERNEL_DS;
}

static inline void set_fs(mm_segment_t val)
{
	current_thread_info()->addr_limit = val.seg;
}

#define segment_eq(a, b)	((a).seg == (b).seg)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TRANSPUTER_SEGMENT_H */
