// SPDX-License-Identifier: GPL-2.0
/* Exercise the actual kernel uaccess object with protected string bounds. */
#include <linux/uaccess.h>
#include <asm/t9000_softprot.h>

#define UART (*(volatile u32 *)0x10000000u)
#define PHYS 0x81000000u
#define LOGICAL 0xc1000000u
#define EFAULT 14

static struct t9k_soft_mm mm;
static struct thread_info thread;
struct thread_info *__current_thread_info = &thread;
static unsigned int failures;
static u32 mapped_base;
static u32 mapped_size;

struct t9k_soft_mm *t9k_task_soft_mm(struct task_struct *task)
{
	return &mm;
}

int t9k_user_mmap_ram(u32 pa, size_t len)
{
	return pa >= mapped_base && len <= mapped_size &&
	       pa - mapped_base <= mapped_size - len;
}

int __get_user_bad(void)
{
	for (;;)
		UART = '!';
}

static void puts_uart(const char *s)
{
	while (*s)
		UART = (unsigned char)*s++;
}

static void expect(const char *name, long got, long want)
{
	if (got != want) {
		puts_uart("FAIL ");
		puts_uart(name);
		puts_uart("\n");
		failures++;
	}
}

static void setup(u32 logical)
{
	struct t9k_soft_region *r = &mm.reg[3];

	r->valid = 1;
	r->phys_base = PHYS;
	r->la_base = logical;
	r->size = 256;
	mm.flags = T9K_SOFT_FL_ACTIVE | T9K_SOFT_FL_SPLIT | T9K_SOFT_FL_PRIV;
	mm.priv_lo = PHYS;
	mm.priv_hi = PHYS + 256;
}

int test_main(void)
{
	char dst[4096];
	char *p = (char *)PHYS;
	const char *src;

	setup(LOGICAL);
	p[253] = 'o'; p[254] = 'k'; p[255] = 0;
	src = (const char *)(LOGICAL + 253);
	expect("region copy", strncpy_from_user(dst, src, 4096), 2);
	expect("region bytes", dst[0] == 'o' && dst[1] == 'k' && !dst[2], 1);
	expect("region length", strnlen_user(src, 4096), 3);
	expect("truncated copy", strncpy_from_user(dst, src, 2), 2);
	expect("truncated length", strnlen_user(src, 2), 3);
	p[255] = 'x';
	expect("region copy fault", strncpy_from_user(dst, src, 4), -EFAULT);
	expect("region length fault", strnlen_user(src, 4), 0);
	expect("null copy", strncpy_from_user(dst, (const char *)0, 1), -EFAULT);
	expect("null length", strnlen_user((const char *)0, 1), 0);
	expect("zero copy", strncpy_from_user(dst, (const char *)0, 0), 0);
	expect("negative length", strnlen_user((const char *)0, -1), 0);
	expect("wrong quarter", strnlen_user((const char *)PHYS, 1), 0);

	mm.priv_hi = PHYS + 16;
	p[14] = 'a'; p[15] = 0; p[16] = 0;
	src = (const char *)(LOGICAL + 14);
	expect("private copy", strncpy_from_user(dst, src, 4096), 1);
	expect("private length", strnlen_user(src, 4096), 2);
	p[15] = 'b';
	expect("private copy fault", strncpy_from_user(dst, src, 4), -EFAULT);
	expect("private length fault", strnlen_user(src, 4), 0);

	mm.stack_base = PHYS + 128;
	mm.stack_len = 16;
	p[142] = 's'; p[143] = 0;
	src = (const char *)(LOGICAL + 142);
	expect("stack copy", strncpy_from_user(dst, src, 4096), 1);
	expect("stack length", strnlen_user(src, 4096), 2);
	mapped_base = PHYS + 192;
	mapped_size = 32;
	p[222] = 'm'; p[223] = 0;
	src = (const char *)(LOGICAL + 222);
	expect("mmap copy", strncpy_from_user(dst, src, 4096), 1);
	expect("mmap length", strnlen_user(src, 4096), 2);

	setup(0xffffff00u);
	p[254] = 'z'; p[255] = 0;
	src = (const char *)0xfffffffeu;
	expect("top copy", strncpy_from_user(dst, src, 4), 1);
	expect("top length", strnlen_user(src, 4), 2);
	p[255] = 'z';
	expect("wrap copy fault", strncpy_from_user(dst, src, 4), -EFAULT);
	expect("wrap length fault", strnlen_user(src, 4), 0);

	mm.flags = 0;
	p[0] = 'i'; p[1] = 0;
	expect("identity copy", strncpy_from_user(dst, p, 8), 1);
	expect("identity length", strnlen_user(p, 8), 2);
	puts_uart(failures ? "USERSTRINGS FAIL\n" : "USERSTRINGS PASS\n");
	return failures != 0;
}
