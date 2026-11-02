/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_UACCESS_H
#define _ASM_TRANSPUTER_UACCESS_H

struct pt_regs;
extern int fixup_exception(struct pt_regs *regs);

/*
 * Transputer uaccess — NOMMU flat by default.
 * When soft-mm is ACTIVE with Q3-remapped data (Phase 9 SPLIT), translate
 * user LAs to phys before touch so trap-syscall path works after leave_p.
 */
#include <asm/t9000_softprot.h>
#include <linux/thread_info.h>
#include <asm/current.h>
#include <linux/unaligned.h>
#include <linux/string.h>

struct task_struct;
struct t9k_soft_mm *t9k_task_soft_mm(struct task_struct *t);

static inline int __t9k_access_ok(unsigned long addr, unsigned long size)
{
	struct t9k_soft_mm *mm = t9k_task_soft_mm(current);

	return t9k_soft_ok(mm, (u32)addr, (size_t)size, 0);
}

static inline void *__t9k_uptr(const void __user *ptr)
{
	struct t9k_soft_mm *mm = t9k_task_soft_mm(current);
	u32 la = (u32)(unsigned long)ptr;

	if (mm && (mm->flags & T9K_SOFT_FL_ACTIVE) &&
	    (mm->flags & T9K_SOFT_FL_SPLIT))
		return (void *)(unsigned long)t9k_la_to_phys(mm, la);
	return (void *)(unsigned long)ptr;
}

static inline long __transputer_strnlen_user(const char __user *s, long n)
{
	const char *p = (const char *)__t9k_uptr(s);
	long len = strnlen(p, n);

	if (len < n)
		len++;
	return len;
}
#define __strnlen_user __transputer_strnlen_user

#define access_ok(addr, size) __t9k_access_ok((unsigned long)(addr), (size))

/* Prefer our translating get/put over UACCESS_MEMCPY identity stores. */
#undef CONFIG_UACCESS_MEMCPY

static __always_inline int
__get_user_fn(size_t size, const void __user *from, void *to)
{
	const void *src = __t9k_uptr(from);

	switch (size) {
	case 1:
		*(u8 *)to = *(const u8 *)src;
		return 0;
	case 2:
		*(u16 *)to = get_unaligned((const u16 *)src);
		return 0;
	case 4:
		*(u32 *)to = get_unaligned((const u32 *)src);
		return 0;
	case 8:
		*(u64 *)to = get_unaligned((const u64 *)src);
		return 0;
	default:
		BUILD_BUG();
		return 0;
	}
}
#define __get_user_fn(sz, u, k)	__get_user_fn(sz, u, k)

static __always_inline int
__put_user_fn(size_t size, void __user *to, void *from)
{
	void *dst = __t9k_uptr(to);

	switch (size) {
	case 1:
		*(u8 *)dst = *(u8 *)from;
		return 0;
	case 2:
		put_unaligned(*(u16 *)from, (u16 *)dst);
		return 0;
	case 4:
		put_unaligned(*(u32 *)from, (u32 *)dst);
		return 0;
	case 8:
		put_unaligned(*(u64 *)from, (u64 *)dst);
		return 0;
	default:
		BUILD_BUG();
		return 0;
	}
}
#define __put_user_fn(sz, u, k)	__put_user_fn(sz, u, k)

static inline unsigned long __clear_user(void __user *to, unsigned long n)
{
	memset(__t9k_uptr(to), 0, n);
	return 0;
}
#define __clear_user __clear_user

unsigned long raw_copy_from_user(void *to, const void __user *from,
				 unsigned long n);
unsigned long raw_copy_to_user(void __user *to, const void *from,
			       unsigned long n);

#include <asm-generic/uaccess.h>

#endif /* _ASM_TRANSPUTER_UACCESS_H */
