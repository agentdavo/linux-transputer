/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer bFLT binary format support
 *
 * Handles two relocation types:
 *   bit 31 clear: standard 32-bit data word relocation
 *   bit 31 set:   LDC prefix sequence (9-byte PFIX chain + LDC opcode)
 *
 * Modelled after ARM and m68k NOMMU implementations.
 */
#ifndef _ASM_TRANSPUTER_FLAT_H
#define _ASM_TRANSPUTER_FLAT_H

#include <linux/unaligned.h>

#define FLAT_RELOC_TYPE_LDC 0x80000000u
#define TP_LDC_SEQ_LEN 9

#define flat_argvp_envp_on_stack()		1
#define flat_old_ram_flag(flags)			(flags)
#define flat_reloc_valid(reloc, size)		(((reloc) & 0x7fffffff) <= (size))
#define flat_set_persistent(relval, p)		0

/* Extract the image offset from a relocation entry, masking off the type flag */
#define flat_get_relocate_addr(rel)		((rel) & 0x7fffffffu)

/* True for PFIX (0x2X) and NFIX (0x6X) prefix bytes. */
static inline int tp_flat_is_prefix(unsigned char b)
{
	return (b & 0xF0) == 0x20 || (b & 0xF0) == 0x60;
}

/*
 * Determine the length (in bytes) of the LDC prefix chain starting at p.
 *
 * The transputer linker emits VARIABLE-LENGTH LDC_ABS sequences: a run of
 * PFIX/NFIX bytes terminated by the LDC opcode. The width depends on how far
 * the linker "walked back" over preceding PFIX 0 padding when patching, so it
 * is NOT a fixed 9 bytes. We must honour the actual width in place, otherwise
 * re-encoding corrupts adjacent code (over-writing the following instruction)
 * or leaves stale prefix bytes that execute as garbage.
 *
 * Bounded by TP_LDC_MAX_LEN as a safety net against a runaway scan over
 * non-LDC data.
 */
#define TP_LDC_MAX_LEN 20
static inline int tp_flat_ldc_len(const unsigned char *p)
{
	int i = 0;
	while (i < TP_LDC_MAX_LEN - 1 && tp_flat_is_prefix(p[i]))
		i++;
	return i + 1;  /* prefixes + the terminating opcode byte */
}

/*
 * Decode a transputer LDC prefix chain of the given length to its operand.
 * Uses hardware semantics:
 *   PFIX n: Oreg = (Oreg | n) << 4
 *   NFIX n: Oreg = ~(Oreg | n) << 4
 *   final:  Operand = Oreg | n
 */
static inline unsigned long tp_flat_decode_ldc_n(const unsigned char *p, int len)
{
	unsigned long oreg = 0;
	int i;
	for (i = 0; i < len - 1; i++) {
		unsigned long nibble = p[i] & 0xF;
		if ((p[i] & 0xF0) == 0x60)  /* NFIX */
			oreg = (~(oreg | nibble)) << 4;
		else  /* PFIX */
			oreg = (oreg | nibble) << 4;
	}
	return oreg | (p[len - 1] & 0xF);
}

static inline unsigned long tp_flat_decode_ldc(const unsigned char *p)
{
	return tp_flat_decode_ldc_n(p, tp_flat_ldc_len(p));
}

/*
 * Re-encode an unsigned value into an EXISTING LDC chain, preserving its
 * width (derived from the current bytes at p). Uses PFIX-only encoding with
 * PFIX 0 left-padding, which represents any 32-bit value given len >= 8.
 */
static inline void tp_flat_encode_ldc(unsigned char *p, unsigned long val)
{
	int len = tp_flat_ldc_len(p);
	int i, needed = 0, pad;
	unsigned long tmp = val;

	/* Count needed nibbles */
	do { needed++; tmp >>= 4; } while (tmp != 0);
	if (needed > len) needed = len;

	pad = len - needed;

	/* Fill prefix positions with PFIX 0 padding */
	for (i = 0; i < len - 1; i++)
		p[i] = 0x20;  /* PFIX 0 */

	/* Encode data nibbles after padding */
	for (i = 0; i < needed - 1; i++) {
		int shift = (needed - 1 - i) * 4;
		p[pad + i] = 0x20 | ((val >> shift) & 0xF);
	}

	/* Final byte: LDC opcode with lowest nibble */
	p[len - 1] = 0x40 | (val & 0xF);
}

/*
 * Read the value at a relocation site.
 *
 * For LDC relocs (bit 31 set in relval): decode the 9-byte prefix sequence.
 * For R32 relocs: read the 32-bit word (already stored big-endian by elf2flt).
 *
 * The caller (binfmt_flat.c) applies ntohl() to the returned value for
 * non-GOTPIC binaries, so LDC values must be returned pre-swapped.
 */
static inline int
flat_get_addr_from_rp(u32 __user *rp, u32 relval, u32 flags, u32 *addr)
{
	if (relval & FLAT_RELOC_TYPE_LDC) {
		u32 value = tp_flat_decode_ldc((const unsigned char __force *)rp);

		/* Generic binfmt_flat applies ntohl() to non-GOTPIC values.
		 * Instruction immediates are already native values, so present
		 * one in big-endian form and let that generic conversion undo it.
		 */
		if (!(flags & FLAT_FLAG_GOTPIC))
			value = ((value & 0x000000ffU) << 24) |
				((value & 0x0000ff00U) << 8) |
				((value & 0x00ff0000U) >> 8) |
				((value & 0xff000000U) >> 24);
		*addr = value;
	} else {
		*addr = get_unaligned((const u32 __force *)rp);
	}
	return 0;
}

/*
 * Write the relocated value back to a relocation site.
 *
 * For LDC relocs: re-encode as a 9-byte prefix sequence.
 * For R32 relocs: write a native-endian 32-bit word.
 */
static inline int
flat_put_addr_at_rp(u32 __user *rp, u32 addr, u32 relval)
{
	if (relval & FLAT_RELOC_TYPE_LDC) {
		tp_flat_encode_ldc((unsigned char __force *)rp, addr);
	} else {
		put_unaligned(addr, (u32 __force *)rp);
	}
	return 0;
}

#endif /* _ASM_TRANSPUTER_FLAT_H */
