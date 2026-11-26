// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is an implementation of the BLAKE2s hash and PRF functions.
 *
 * Information: https://blake2.net/
 *
 */

#include <crypto/blake2s.h>
#include <linux/bug.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unroll.h>
#include <linux/types.h>

/* Message word permutation for each of the ten rounds. */
#define BLAKE2S_SIGMA_0(ROW) ROW(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)
#define BLAKE2S_SIGMA_1(ROW) ROW(14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3)
#define BLAKE2S_SIGMA_2(ROW) ROW(11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4)
#define BLAKE2S_SIGMA_3(ROW) ROW(7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8)
#define BLAKE2S_SIGMA_4(ROW) ROW(9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13)
#define BLAKE2S_SIGMA_5(ROW) ROW(2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9)
#define BLAKE2S_SIGMA_6(ROW) ROW(12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11)
#define BLAKE2S_SIGMA_7(ROW) ROW(13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10)
#define BLAKE2S_SIGMA_8(ROW) ROW(6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5)
#define BLAKE2S_SIGMA_9(ROW) ROW(10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0)

#ifdef __TCC_TRANSPUTER_ROTATE32__
#define ror32(word, shift)	__builtin_rotateright32((word), (shift))
#else
#define SIGMA_VALUES(...) { __VA_ARGS__ }
static const u8 blake2s_sigma[10][16] = {
	BLAKE2S_SIGMA_0(SIGMA_VALUES),
	BLAKE2S_SIGMA_1(SIGMA_VALUES),
	BLAKE2S_SIGMA_2(SIGMA_VALUES),
	BLAKE2S_SIGMA_3(SIGMA_VALUES),
	BLAKE2S_SIGMA_4(SIGMA_VALUES),
	BLAKE2S_SIGMA_5(SIGMA_VALUES),
	BLAKE2S_SIGMA_6(SIGMA_VALUES),
	BLAKE2S_SIGMA_7(SIGMA_VALUES),
	BLAKE2S_SIGMA_8(SIGMA_VALUES),
	BLAKE2S_SIGMA_9(SIGMA_VALUES),
};
#undef SIGMA_VALUES
#endif

static inline void blake2s_increment_counter(struct blake2s_ctx *ctx, u32 inc)
{
	ctx->t[0] += inc;
	ctx->t[1] += (ctx->t[0] < inc);
}

static void __maybe_unused
blake2s_compress_generic(struct blake2s_ctx *ctx,
			 const u8 *data, size_t nblocks, u32 inc)
{
	u32 m[16];
	u32 v[16];
	int i;

	WARN_ON(IS_ENABLED(DEBUG) &&
		(nblocks > 1 && inc != BLAKE2S_BLOCK_SIZE));

	while (nblocks > 0) {
		blake2s_increment_counter(ctx, inc);
		memcpy(m, data, BLAKE2S_BLOCK_SIZE);
		le32_to_cpu_array(m, ARRAY_SIZE(m));
		memcpy(v, ctx->h, 32);
		v[ 8] = BLAKE2S_IV0;
		v[ 9] = BLAKE2S_IV1;
		v[10] = BLAKE2S_IV2;
		v[11] = BLAKE2S_IV3;
		v[12] = BLAKE2S_IV4 ^ ctx->t[0];
		v[13] = BLAKE2S_IV5 ^ ctx->t[1];
		v[14] = BLAKE2S_IV6 ^ ctx->f[0];
		v[15] = BLAKE2S_IV7 ^ ctx->f[1];

#define G(x, y, a, b, c, d) do { \
	a += b + m[x]; \
	d = ror32(d ^ a, 16); \
	c += d; \
	b = ror32(b ^ c, 12); \
	a += b + m[y]; \
	d = ror32(d ^ a, 8); \
	c += d; \
	b = ror32(b ^ c, 7); \
} while (0)

#define ROUND(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, sa, sb, sc, sd, se, sf) do { \
	G(s0, s1, v[0], v[4], v[8], v[12]); \
	G(s2, s3, v[1], v[5], v[9], v[13]); \
	G(s4, s5, v[2], v[6], v[10], v[14]); \
	G(s6, s7, v[3], v[7], v[11], v[15]); \
	G(s8, s9, v[0], v[5], v[10], v[15]); \
	G(sa, sb, v[1], v[6], v[11], v[12]); \
	G(sc, sd, v[2], v[7], v[8], v[13]); \
	G(se, sf, v[3], v[4], v[9], v[14]); \
} while (0)

#ifdef __TCC_TRANSPUTER_ROTATE32__
		/* TinyCC does not implement the unroll pragma. Expand the same
		 * schedule with constant offsets into the workspace array.
		 */
		BLAKE2S_SIGMA_0(ROUND);
		BLAKE2S_SIGMA_1(ROUND);
		BLAKE2S_SIGMA_2(ROUND);
		BLAKE2S_SIGMA_3(ROUND);
		BLAKE2S_SIGMA_4(ROUND);
		BLAKE2S_SIGMA_5(ROUND);
		BLAKE2S_SIGMA_6(ROUND);
		BLAKE2S_SIGMA_7(ROUND);
		BLAKE2S_SIGMA_8(ROUND);
		BLAKE2S_SIGMA_9(ROUND);
#else
		/*
		 * Unroll the rounds loop to enable constant-folding of the
		 * blake2s_sigma values.
		 */
		unrolled_full
		for (int r = 0; r < 10; r++) {
			const u8 *s = blake2s_sigma[r];

			ROUND(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7],
			      s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]);
		}
#endif
#undef ROUND
#undef G

		for (i = 0; i < 8; ++i)
			ctx->h[i] ^= v[i] ^ v[i + 8];

		data += BLAKE2S_BLOCK_SIZE;
		--nblocks;
	}
}

#ifdef CONFIG_CRYPTO_LIB_BLAKE2S_ARCH
#include "blake2s.h" /* $(SRCARCH)/blake2s.h */
#else
#define blake2s_compress blake2s_compress_generic
#endif

static inline void blake2s_set_lastblock(struct blake2s_ctx *ctx)
{
	ctx->f[0] = -1;
}

void blake2s_update(struct blake2s_ctx *ctx, const u8 *in, size_t inlen)
{
	const size_t fill = BLAKE2S_BLOCK_SIZE - ctx->buflen;

	if (unlikely(!inlen))
		return;
	if (inlen > fill) {
		memcpy(ctx->buf + ctx->buflen, in, fill);
		blake2s_compress(ctx, ctx->buf, 1, BLAKE2S_BLOCK_SIZE);
		ctx->buflen = 0;
		in += fill;
		inlen -= fill;
	}
	if (inlen > BLAKE2S_BLOCK_SIZE) {
		const size_t nblocks = DIV_ROUND_UP(inlen, BLAKE2S_BLOCK_SIZE);

		blake2s_compress(ctx, in, nblocks - 1, BLAKE2S_BLOCK_SIZE);
		in += BLAKE2S_BLOCK_SIZE * (nblocks - 1);
		inlen -= BLAKE2S_BLOCK_SIZE * (nblocks - 1);
	}
	memcpy(ctx->buf + ctx->buflen, in, inlen);
	ctx->buflen += inlen;
}
EXPORT_SYMBOL(blake2s_update);

void blake2s_final(struct blake2s_ctx *ctx, u8 *out)
{
	WARN_ON(IS_ENABLED(DEBUG) && !out);
	blake2s_set_lastblock(ctx);
	memset(ctx->buf + ctx->buflen, 0,
	       BLAKE2S_BLOCK_SIZE - ctx->buflen); /* Padding */
	blake2s_compress(ctx, ctx->buf, 1, ctx->buflen);
	cpu_to_le32_array(ctx->h, ARRAY_SIZE(ctx->h));
	memcpy(out, ctx->h, ctx->outlen);
	blake2s_zeroize_ctx(ctx);
}
EXPORT_SYMBOL(blake2s_final);

#ifdef blake2s_mod_init_arch
static int __init blake2s_mod_init(void)
{
	blake2s_mod_init_arch();
	return 0;
}
subsys_initcall(blake2s_mod_init);
#endif

MODULE_DESCRIPTION("BLAKE2s hash function");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
