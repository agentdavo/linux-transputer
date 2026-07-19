// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer user space access functions
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/string.h>

/*
 * Simple implementation of raw_copy_from_user for transputer.
 * Since transputer has no MMU, user/kernel space is the same.
 */
unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	memcpy(to, (const void *)from, n);
	return 0;
}
EXPORT_SYMBOL(raw_copy_from_user);

/*
 * Simple implementation of raw_copy_to_user for transputer.
 */
unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	memcpy((void *)to, from, n);
	return 0;
}
EXPORT_SYMBOL(raw_copy_to_user);
