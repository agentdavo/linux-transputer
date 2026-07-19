#ifndef _ASM_TRANSPUTER_DIV64_H
#define _ASM_TRANSPUTER_DIV64_H

#include <linux/types.h>
#include <linux/compiler.h>

/*
 * TCC does not parse the asm-generic type-safety check used by do_div()
 * on 32-bit targets. Keep the same logic but drop the typeof pointer test.
 */
#if BITS_PER_LONG == 64
#define do_div(n, base) ({					\
	u32 __base = (base);					\
	u32 __rem;						\
	__rem = ((u64)(n)) % __base;				\
	(n) = ((u64)(n)) / __base;				\
	__rem;							\
})
#elif BITS_PER_LONG == 32
extern u32 __div64_32(u64 *dividend, u32 divisor);
#define do_div(n, base) ({					\
	u32 __base = (base);					\
	u32 __rem;						\
	if (likely(((n) >> 32) == 0)) {				\
		__rem = (u32)(n) % __base;			\
		(n) = (u32)(n) / __base;				\
	} else {						\
		__rem = __div64_32(&(n), __base);		\
	}							\
	__rem;							\
})
#else
#error do_div() does not support this BITS_PER_LONG
#endif

#endif
