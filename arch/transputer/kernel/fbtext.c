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

/* The four pixels of each 4-bit slice of a font row, high bit leftmost. */
#define FBT_PX(n, b)	((((n) >> (b)) & 1) ? FBT_FG : FBT_BG)
#define FBT_NIB(n)	{ FBT_PX(n, 3), FBT_PX(n, 2), FBT_PX(n, 1), FBT_PX(n, 0) }
static const u32 fbt_nib[16][4] = {
	FBT_NIB(0),  FBT_NIB(1),  FBT_NIB(2),  FBT_NIB(3),
	FBT_NIB(4),  FBT_NIB(5),  FBT_NIB(6),  FBT_NIB(7),
	FBT_NIB(8),  FBT_NIB(9),  FBT_NIB(10), FBT_NIB(11),
	FBT_NIB(12), FBT_NIB(13), FBT_NIB(14), FBT_NIB(15),
};

/*
 * The boot spends most of its console time here, so both loops are written
 * for the transputer: eight word stores a pass through a pointer, and a
 * nonblank glyph row as two table lookups and eight copies, with no
 * per-pixel shift, test or branch.  No MOVE, which is multi-cycle on the RTL.
 */
static void fbt_clear_row(int row)
{
	u32 *p = (u32 *)TRANSPUTER_FB_BASE +
		 (u32)row * 16u * TRANSPUTER_FB_WIDTH;
	u32 *end = p + 16u * TRANSPUTER_FB_WIDTH;	/* a multiple of 8 */

	for (; p != end; p += 8) {
		p[0] = FBT_BG; p[1] = FBT_BG; p[2] = FBT_BG; p[3] = FBT_BG;
		p[4] = FBT_BG; p[5] = FBT_BG; p[6] = FBT_BG; p[7] = FBT_BG;
	}
}

static void fbt_glyph(int col, int row, unsigned char c)
{
	volatile u32 *p;
	const unsigned char *g, *end;

	if (c < 32 || c > 126)
		c = ' ';
	g = fbtext_font[c - 32];
	p = (volatile u32 *)TRANSPUTER_FB_BASE +
	    (u32)row * 16u * TRANSPUTER_FB_WIDTH + (u32)col * 8u;
	end = g + 16;
	for (; g != end; p += TRANSPUTER_FB_WIDTH) {
		unsigned int bits = *g++;
		const u32 *hi, *lo;

		if (!bits) {
			p[0] = FBT_BG; p[1] = FBT_BG; p[2] = FBT_BG; p[3] = FBT_BG;
			p[4] = FBT_BG; p[5] = FBT_BG; p[6] = FBT_BG; p[7] = FBT_BG;
			continue;
		}

		hi = fbt_nib[bits >> 4];
		lo = fbt_nib[bits & 15];
		p[0] = hi[0]; p[1] = hi[1]; p[2] = hi[2]; p[3] = hi[3];
		p[4] = lo[0]; p[5] = lo[1]; p[6] = lo[2]; p[7] = lo[3];
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
