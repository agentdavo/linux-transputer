// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer user space access — LA→phys when soft-mm SPLIT is ACTIVE.
 */
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <asm/t9000_softprot.h>
#include <asm/current.h>

static inline void *t9k_user_ptr(const void __user *from)
{
	struct t9k_soft_mm *mm = t9k_task_soft_mm(current);
	u32 la = (u32)(unsigned long)from;

	if (mm && (mm->flags & T9K_SOFT_FL_ACTIVE) &&
	    (mm->flags & T9K_SOFT_FL_SPLIT))
		return (void *)(unsigned long)t9k_la_to_phys(mm, la);
	return (void *)(unsigned long)from;
}

unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	memcpy(to, t9k_user_ptr(from), n);
	return 0;
}
EXPORT_SYMBOL(raw_copy_from_user);

unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	memcpy(t9k_user_ptr(to), from, n);
	return 0;
}
EXPORT_SYMBOL(raw_copy_to_user);
