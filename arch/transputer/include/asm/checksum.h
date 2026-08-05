#ifndef _ASM_TRANSPUTER_CHECKSUM_H
#define _ASM_TRANSPUTER_CHECKSUM_H

/*
 * Internet checksum via the TVX CSUM instruction (OPR 0x1F3).
 *
 * __csumb pairs bytes into little-endian halfwords starting at the
 * given pointer regardless of alignment - the same byte-stream
 * contract the asm-generic do_csum implements in software (its odd-
 * alignment path is an internal word-access optimisation, not a
 * semantic difference).  The folded 16-bit result is a valid
 * ones-complement partial sum: folding early is congruent, and
 * csum_add/csum_fold compose with it unchanged.
 *
 * Measured on the T9000 model: 1500-byte frame 15,852 -> 156 cycles.
 */

unsigned int __csumb(const void *src, unsigned int init,
                     unsigned int nbytes);

#define do_csum do_csum
static inline unsigned int do_csum(const unsigned char *buff, int len)
{
	if (len <= 0)
		return 0;
	return __csumb(buff, 0u, (unsigned int)len);
}

#include <asm-generic/checksum.h>

#endif /* _ASM_TRANSPUTER_CHECKSUM_H */
