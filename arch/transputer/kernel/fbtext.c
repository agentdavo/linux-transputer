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

static void fbt_clear_row(int row)
{
	volatile u32 *fb = (volatile u32 *)TRANSPUTER_FB_BASE;
	u32 *p = (u32 *)fb + (u32)row * 16u * TRANSPUTER_FB_WIDTH;
	unsigned int i;

	for (i = 0; i < 16u * TRANSPUTER_FB_WIDTH; i++)
		p[i] = FBT_BG;
}

static void fbt_glyph(int col, int row, unsigned char c)
{
	volatile u32 *fb = (volatile u32 *)TRANSPUTER_FB_BASE;
	const unsigned char *g;
	unsigned int y, x;

	if (c < 32 || c > 126)
		c = ' ';
	g = fbtext_font[c - 32];
	for (y = 0; y < 16; y++) {
		volatile u32 *p = fb +
			((u32)row * 16u + y) * TRANSPUTER_FB_WIDTH +
			(u32)col * 8u;
		unsigned char bits = g[y];
		for (x = 0; x < 8; x++)
			p[x] = (bits & (0x80u >> x)) ? FBT_FG : FBT_BG;
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
