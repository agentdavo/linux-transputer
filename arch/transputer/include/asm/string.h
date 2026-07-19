/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_STRING_H
#define _ASM_TRANSPUTER_STRING_H

/*
 * Transputer-specific string functions.
 *
 * Implemented as static inline to avoid TCC cross-file linker bug
 * that resolves extern calls to wrong addresses in large binaries.
 * Each compilation unit gets its own copy — no cross-file linking needed.
 */

/*
 * clear_page_move (page.S): zero 4 KiB via MOVE doubling (~2k cycles vs
 * ~90k for a per-word stnl loop).  Critical on CHDL after zonelists when
 * free memory is bulk-zeroed.
 */
extern void clear_page_move(void *page);

#define __HAVE_ARCH_MEMSET
static inline void *memset(void *s, int c, __kernel_size_t count)
{
	unsigned char *p = (unsigned char *)s;
	unsigned char val = (unsigned char)c;
	__kernel_size_t i;

	if (val == 0) {
		/* Align to word */
		while (count && ((unsigned long)p & 3)) {
			*p++ = 0;
			count--;
		}
		/* Page-aligned bulk zero via MOVE doubling */
		while (count >= 4096 && (((unsigned long)p & 4095) == 0)) {
			clear_page_move(p);
			p += 4096;
			count -= 4096;
		}
		/* Word fill remainder */
		if (count >= 4) {
			unsigned long *wp = (unsigned long *)p;
			__kernel_size_t wcount = count >> 2;

			for (i = 0; i < wcount; i++)
				wp[i] = 0;
			p += wcount << 2;
			count -= wcount << 2;
		}
		while (count--)
			*p++ = 0;
		return s;
	}

	for (i = 0; i < count; i++)
		p[i] = val;
	return s;
}

#define __HAVE_ARCH_MEMCPY
static inline void *memcpy(void *dest, const void *src, __kernel_size_t count)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	__kernel_size_t i;
	for (i = 0; i < count; i++)
		d[i] = s[i];
	return dest;
}

#define __HAVE_ARCH_MEMMOVE
static inline void *memmove(void *dest, const void *src, __kernel_size_t count)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	__kernel_size_t i;
	if (d <= s) {
		for (i = 0; i < count; i++)
			d[i] = s[i];
	} else {
		i = count;
		while (i > 0) {
			i--;
			d[i] = s[i];
		}
	}
	return dest;
}

#define __HAVE_ARCH_STRCMP
static inline int strcmp(const char *cs, const char *ct)
{
	while (*cs && *cs == *ct) {
		cs++;
		ct++;
	}
	return (unsigned char)*cs - (unsigned char)*ct;
}

#define __HAVE_ARCH_STRCPY
static inline char *strcpy(char *dest, const char *src)
{
	char *d = dest;
	while ((*d++ = *src++) != '\0')
		;
	return dest;
}

#define __HAVE_ARCH_STRLEN
static inline __kernel_size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return p - s;
}

#define __HAVE_ARCH_STRNLEN
static inline __kernel_size_t strnlen(const char *s, __kernel_size_t count)
{
	const char *sc;
	/* Safety: cap scan length to prevent hangs on garbage pointers */
	if (count > 4096)
		count = 4096;
	for (sc = s; count-- && *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}

#endif /* _ASM_TRANSPUTER_STRING_H */
