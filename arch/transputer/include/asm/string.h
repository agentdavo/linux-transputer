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
 * clear_page_move (clear_page.c): zero 4 KiB via word stores.
 * (Old MOVE-doubling in page.S was T4-oriented; SPINE RTL multi-cycle MOVE
 *  made early boot look frozen in clear_loop.)
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

/*
 * KNOWN COST, NOT YET FIXED - see the note below before "improving" this.
 *
 * This byte loop compiles to ~23 instructions per byte (ldl/ldl/bsub/ldl/ldl/
 * bsub/lb/ldc 255/and/rev/sb, plus the loop head and increment), where the
 * T800 MOVE instruction (#4A) copies the whole block in one.  Measured on a
 * boot: restoring the 867 KB busybox image on every applet spawn - a single
 * memcpy - costs 38 M cycles, and five spawns plus the SD-card init make it
 * the largest single cost in the system.  Running `true` costs 1/4000th of
 * getting to it.
 *
 * Two fast paths already exist and are unreachable *because* of this
 * definition: arch/transputer/lib/memcpy.S implements memcpy with MOVE, and
 * the compiler's tp_try_inline_memcpy() turns a *call* to memcpy into an
 * inline MOVE.  A static inline here means there is never a call to
 * intercept.  (memset already avoids this via clear_page_move().)
 *
 * Switching to `extern void *memcpy(...)` plus memcpy.o in lib-y was tried on
 * 2026-08-05.  It builds, links cleanly, and is *functionally correct* -
 * c-testsuite 09099 exercises the MOVE path across sizes, alignments, zero
 * length, return-value use and awkward argument shapes, and passes.  But the
 * kernel drops from 13 MB to 7.5 MB (the inline was duplicated at every struct
 * assignment), every address moves, and the boot then hangs immediately after
 * "VFS: Finished mounting rootfs".  That smells like the known layout
 * sensitivity in this tree rather than anything wrong with MOVE.  Reverted
 * pending a bisect; the win is worth ~35% of boot, so it is worth doing
 * properly.
 */
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
