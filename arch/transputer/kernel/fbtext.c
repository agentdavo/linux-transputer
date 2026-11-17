/*
 * fbtext.c — minimal kernel text renderer on the simplefb surface.
 *
 * Mirrors every console byte onto the framebuffer (8x16 font, XRGB8888)
 * so a simulated boot can be watched as framebuffer snapshots.  Ring
 * rendering instead of scrolling: on reaching the bottom the cursor wraps
 * to the top and the next row is cleared first (a full-screen scroll copy
 * is ~1.9 MB per line — pathological under RTL simulation).
 *
 * The surface is plain RAM (asm/simplefb.h carve), safe to write from the
 * first printk.  fbwm later takes the screen over completely — its first
 * full-frame present simply overwrites us.  Disable with "nofbtext".
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/simplefb.h>
#include "fbtext_font.h"

#define FBT_W    (TRANSPUTER_FB_WIDTH / 8)    /* columns  */
#define FBT_H    (TRANSPUTER_FB_HEIGHT / 16)  /* rows     */
#define FBT_FG   0x00C0C0C0u
#define FBT_BG   0x00000000u

static int fbt_col, fbt_row;
static int fbt_off;   /* "nofbtext" */

/* Expand each font byte to eight XRGB pixels, high bit leftmost. */
#define FBT_PX(n, b)	((((n) >> (b)) & 1) ? FBT_FG : FBT_BG)
#define FBT_PIXELS(n) { \
	FBT_PX(n, 7), FBT_PX(n, 6), FBT_PX(n, 5), FBT_PX(n, 4), \
	FBT_PX(n, 3), FBT_PX(n, 2), FBT_PX(n, 1), FBT_PX(n, 0) }
static const u32 fbt_pixels[256][8] = {
	FBT_PIXELS(0x00), FBT_PIXELS(0x01), FBT_PIXELS(0x02), FBT_PIXELS(0x03),
	FBT_PIXELS(0x04), FBT_PIXELS(0x05), FBT_PIXELS(0x06), FBT_PIXELS(0x07),
	FBT_PIXELS(0x08), FBT_PIXELS(0x09), FBT_PIXELS(0x0a), FBT_PIXELS(0x0b),
	FBT_PIXELS(0x0c), FBT_PIXELS(0x0d), FBT_PIXELS(0x0e), FBT_PIXELS(0x0f),
	FBT_PIXELS(0x10), FBT_PIXELS(0x11), FBT_PIXELS(0x12), FBT_PIXELS(0x13),
	FBT_PIXELS(0x14), FBT_PIXELS(0x15), FBT_PIXELS(0x16), FBT_PIXELS(0x17),
	FBT_PIXELS(0x18), FBT_PIXELS(0x19), FBT_PIXELS(0x1a), FBT_PIXELS(0x1b),
	FBT_PIXELS(0x1c), FBT_PIXELS(0x1d), FBT_PIXELS(0x1e), FBT_PIXELS(0x1f),
	FBT_PIXELS(0x20), FBT_PIXELS(0x21), FBT_PIXELS(0x22), FBT_PIXELS(0x23),
	FBT_PIXELS(0x24), FBT_PIXELS(0x25), FBT_PIXELS(0x26), FBT_PIXELS(0x27),
	FBT_PIXELS(0x28), FBT_PIXELS(0x29), FBT_PIXELS(0x2a), FBT_PIXELS(0x2b),
	FBT_PIXELS(0x2c), FBT_PIXELS(0x2d), FBT_PIXELS(0x2e), FBT_PIXELS(0x2f),
	FBT_PIXELS(0x30), FBT_PIXELS(0x31), FBT_PIXELS(0x32), FBT_PIXELS(0x33),
	FBT_PIXELS(0x34), FBT_PIXELS(0x35), FBT_PIXELS(0x36), FBT_PIXELS(0x37),
	FBT_PIXELS(0x38), FBT_PIXELS(0x39), FBT_PIXELS(0x3a), FBT_PIXELS(0x3b),
	FBT_PIXELS(0x3c), FBT_PIXELS(0x3d), FBT_PIXELS(0x3e), FBT_PIXELS(0x3f),
	FBT_PIXELS(0x40), FBT_PIXELS(0x41), FBT_PIXELS(0x42), FBT_PIXELS(0x43),
	FBT_PIXELS(0x44), FBT_PIXELS(0x45), FBT_PIXELS(0x46), FBT_PIXELS(0x47),
	FBT_PIXELS(0x48), FBT_PIXELS(0x49), FBT_PIXELS(0x4a), FBT_PIXELS(0x4b),
	FBT_PIXELS(0x4c), FBT_PIXELS(0x4d), FBT_PIXELS(0x4e), FBT_PIXELS(0x4f),
	FBT_PIXELS(0x50), FBT_PIXELS(0x51), FBT_PIXELS(0x52), FBT_PIXELS(0x53),
	FBT_PIXELS(0x54), FBT_PIXELS(0x55), FBT_PIXELS(0x56), FBT_PIXELS(0x57),
	FBT_PIXELS(0x58), FBT_PIXELS(0x59), FBT_PIXELS(0x5a), FBT_PIXELS(0x5b),
	FBT_PIXELS(0x5c), FBT_PIXELS(0x5d), FBT_PIXELS(0x5e), FBT_PIXELS(0x5f),
	FBT_PIXELS(0x60), FBT_PIXELS(0x61), FBT_PIXELS(0x62), FBT_PIXELS(0x63),
	FBT_PIXELS(0x64), FBT_PIXELS(0x65), FBT_PIXELS(0x66), FBT_PIXELS(0x67),
	FBT_PIXELS(0x68), FBT_PIXELS(0x69), FBT_PIXELS(0x6a), FBT_PIXELS(0x6b),
	FBT_PIXELS(0x6c), FBT_PIXELS(0x6d), FBT_PIXELS(0x6e), FBT_PIXELS(0x6f),
	FBT_PIXELS(0x70), FBT_PIXELS(0x71), FBT_PIXELS(0x72), FBT_PIXELS(0x73),
	FBT_PIXELS(0x74), FBT_PIXELS(0x75), FBT_PIXELS(0x76), FBT_PIXELS(0x77),
	FBT_PIXELS(0x78), FBT_PIXELS(0x79), FBT_PIXELS(0x7a), FBT_PIXELS(0x7b),
	FBT_PIXELS(0x7c), FBT_PIXELS(0x7d), FBT_PIXELS(0x7e), FBT_PIXELS(0x7f),
	FBT_PIXELS(0x80), FBT_PIXELS(0x81), FBT_PIXELS(0x82), FBT_PIXELS(0x83),
	FBT_PIXELS(0x84), FBT_PIXELS(0x85), FBT_PIXELS(0x86), FBT_PIXELS(0x87),
	FBT_PIXELS(0x88), FBT_PIXELS(0x89), FBT_PIXELS(0x8a), FBT_PIXELS(0x8b),
	FBT_PIXELS(0x8c), FBT_PIXELS(0x8d), FBT_PIXELS(0x8e), FBT_PIXELS(0x8f),
	FBT_PIXELS(0x90), FBT_PIXELS(0x91), FBT_PIXELS(0x92), FBT_PIXELS(0x93),
	FBT_PIXELS(0x94), FBT_PIXELS(0x95), FBT_PIXELS(0x96), FBT_PIXELS(0x97),
	FBT_PIXELS(0x98), FBT_PIXELS(0x99), FBT_PIXELS(0x9a), FBT_PIXELS(0x9b),
	FBT_PIXELS(0x9c), FBT_PIXELS(0x9d), FBT_PIXELS(0x9e), FBT_PIXELS(0x9f),
	FBT_PIXELS(0xa0), FBT_PIXELS(0xa1), FBT_PIXELS(0xa2), FBT_PIXELS(0xa3),
	FBT_PIXELS(0xa4), FBT_PIXELS(0xa5), FBT_PIXELS(0xa6), FBT_PIXELS(0xa7),
	FBT_PIXELS(0xa8), FBT_PIXELS(0xa9), FBT_PIXELS(0xaa), FBT_PIXELS(0xab),
	FBT_PIXELS(0xac), FBT_PIXELS(0xad), FBT_PIXELS(0xae), FBT_PIXELS(0xaf),
	FBT_PIXELS(0xb0), FBT_PIXELS(0xb1), FBT_PIXELS(0xb2), FBT_PIXELS(0xb3),
	FBT_PIXELS(0xb4), FBT_PIXELS(0xb5), FBT_PIXELS(0xb6), FBT_PIXELS(0xb7),
	FBT_PIXELS(0xb8), FBT_PIXELS(0xb9), FBT_PIXELS(0xba), FBT_PIXELS(0xbb),
	FBT_PIXELS(0xbc), FBT_PIXELS(0xbd), FBT_PIXELS(0xbe), FBT_PIXELS(0xbf),
	FBT_PIXELS(0xc0), FBT_PIXELS(0xc1), FBT_PIXELS(0xc2), FBT_PIXELS(0xc3),
	FBT_PIXELS(0xc4), FBT_PIXELS(0xc5), FBT_PIXELS(0xc6), FBT_PIXELS(0xc7),
	FBT_PIXELS(0xc8), FBT_PIXELS(0xc9), FBT_PIXELS(0xca), FBT_PIXELS(0xcb),
	FBT_PIXELS(0xcc), FBT_PIXELS(0xcd), FBT_PIXELS(0xce), FBT_PIXELS(0xcf),
	FBT_PIXELS(0xd0), FBT_PIXELS(0xd1), FBT_PIXELS(0xd2), FBT_PIXELS(0xd3),
	FBT_PIXELS(0xd4), FBT_PIXELS(0xd5), FBT_PIXELS(0xd6), FBT_PIXELS(0xd7),
	FBT_PIXELS(0xd8), FBT_PIXELS(0xd9), FBT_PIXELS(0xda), FBT_PIXELS(0xdb),
	FBT_PIXELS(0xdc), FBT_PIXELS(0xdd), FBT_PIXELS(0xde), FBT_PIXELS(0xdf),
	FBT_PIXELS(0xe0), FBT_PIXELS(0xe1), FBT_PIXELS(0xe2), FBT_PIXELS(0xe3),
	FBT_PIXELS(0xe4), FBT_PIXELS(0xe5), FBT_PIXELS(0xe6), FBT_PIXELS(0xe7),
	FBT_PIXELS(0xe8), FBT_PIXELS(0xe9), FBT_PIXELS(0xea), FBT_PIXELS(0xeb),
	FBT_PIXELS(0xec), FBT_PIXELS(0xed), FBT_PIXELS(0xee), FBT_PIXELS(0xef),
	FBT_PIXELS(0xf0), FBT_PIXELS(0xf1), FBT_PIXELS(0xf2), FBT_PIXELS(0xf3),
	FBT_PIXELS(0xf4), FBT_PIXELS(0xf5), FBT_PIXELS(0xf6), FBT_PIXELS(0xf7),
	FBT_PIXELS(0xf8), FBT_PIXELS(0xf9), FBT_PIXELS(0xfa), FBT_PIXELS(0xfb),
	FBT_PIXELS(0xfc), FBT_PIXELS(0xfd), FBT_PIXELS(0xfe), FBT_PIXELS(0xff),
};

/* Both the store offsets and the pointer advances fit in one byte. */
#define FBT_CLEAR_5(p, n) do { \
	(p)[(n) + 0] = FBT_BG; \
	(p)[(n) + 1] = FBT_BG; \
	(p)[(n) + 2] = FBT_BG; \
	(p)[(n) + 3] = FBT_BG; \
	(p)[(n) + 4] = FBT_BG; \
} while (0)

#define FBT_CLEAR_15(p) do { \
	FBT_CLEAR_5(p, 0); \
	FBT_CLEAR_5(p, 5); \
	FBT_CLEAR_5(p, 10); \
} while (0)

static void fbt_clear_row(int row)
{
	u32 *p = (u32 *)TRANSPUTER_FB_BASE +
		 (u32)row * 16u * TRANSPUTER_FB_WIDTH;
	u32 *end = p + 16u * TRANSPUTER_FB_WIDTH;

	BUILD_BUG_ON((16u * TRANSPUTER_FB_WIDTH) % 160);

	/* Ten 15-pixel groups and a 10-pixel tail per branch. */
	do {
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_15(p);
		p += 15;
		FBT_CLEAR_5(p, 0);
		FBT_CLEAR_5(p, 5);
		p += 10;
	} while (p != end);
}

#undef FBT_CLEAR_15
#undef FBT_CLEAR_5

static void fbt_glyph(int col, int row, unsigned char c)
{
	volatile u32 *p;
	const unsigned char *g, *end;

	if (c < 32 || c > 126)
		c = ' ';
	p = (volatile u32 *)TRANSPUTER_FB_BASE +
	    (u32)row * 16u * TRANSPUTER_FB_WIDTH + (u32)col * 8u;
	if (c == ' ') {
		volatile u32 *bottom = p + 16u * TRANSPUTER_FB_WIDTH;

		/* The space glyph is blank on every scanline. */
		do {
			p[0] = FBT_BG; p[1] = FBT_BG; p[2] = FBT_BG; p[3] = FBT_BG;
			p[4] = FBT_BG; p[5] = FBT_BG; p[6] = FBT_BG; p[7] = FBT_BG;
			p += TRANSPUTER_FB_WIDTH;
		} while (p != bottom);
		return;
	}
	g = fbtext_font[c - 32];
	end = g + 16;
	for (; g != end; p += TRANSPUTER_FB_WIDTH) {
		unsigned int bits = *g++;
		const u32 *pixels;

		if (!bits) {
			p[0] = FBT_BG; p[1] = FBT_BG; p[2] = FBT_BG; p[3] = FBT_BG;
			p[4] = FBT_BG; p[5] = FBT_BG; p[6] = FBT_BG; p[7] = FBT_BG;
			continue;
		}

		pixels = fbt_pixels[bits];
		p[0] = pixels[0]; p[1] = pixels[1]; p[2] = pixels[2]; p[3] = pixels[3];
		p[4] = pixels[4]; p[5] = pixels[5]; p[6] = pixels[6]; p[7] = pixels[7];
	}
}

static void fbt_newline(void)
{
	fbt_col = 0;
	fbt_row++;
	if (fbt_row >= FBT_H)
		fbt_row = 0;
	fbt_clear_row(fbt_row);
}

void fbtext_putc(char c)
{
	if (fbt_off)
		return;
	if (c == '\n') {
		fbt_newline();
		return;
	}
	if (c == '\r')
		return;
	fbt_glyph(fbt_col, fbt_row, (unsigned char)c);
	fbt_col++;
	if (fbt_col >= FBT_W)
		fbt_newline();
}

static int __init fbtext_setup(char *str)
{
	fbt_off = 1;
	return 1;
}
__setup("nofbtext", fbtext_setup);
