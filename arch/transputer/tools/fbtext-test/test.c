// SPDX-License-Identifier: GPL-2.0
/*
 * Bare-metal oracle for the kernel console object, including its ring cursor.
 * Volatile accesses force each observed pixel and MMIO UART byte to memory.
 */
#include "../../kernel/fbtext_font.h"

#define UART (*(volatile unsigned int *)0x10000000u)
#define FB ((volatile unsigned int *)0x81000000u)
#define EXPECTED ((volatile unsigned int *)0x81400000u)
#define CAPTURE ((volatile unsigned int *)0x81e00000u)
#define WIDTH 800u
#define HEIGHT 600u
#define PIXELS (WIDTH * HEIGHT)
#define SENTINEL 0xa5a5a5a5u

extern void fbtext_putc(char c);

static unsigned int column;
static unsigned int row;
static unsigned int failures;

static void puts_uart(const char *text)
{
	while (*text)
		UART = (unsigned char)*text++;
}

static void hex_uart(unsigned int value)
{
	const char *digits = "0123456789abcdef";
	int shift;

	for (shift = 28; shift >= 0; shift -= 4)
		UART = digits[(value >> shift) & 15];
}

static void next_row(void)
{
	unsigned int i;

	column = 0;
	row++;
	if (row == HEIGHT / 16)
		row = 0;
	for (i = 0; i < 16 * WIDTH; i++)
		EXPECTED[row * 16 * WIDTH + i] = 0;
}

static void reference(unsigned int c)
{
	unsigned int x, y;

	if (c == 10) {
		next_row();
		return;
	}
	if (c == 13)
		return;
	if (c < 32 || c > 126)
		c = 32;
	for (y = 0; y < 16; y++) {
		unsigned int bits = fbtext_font[c - 32][y];

		for (x = 0; x < 8; x++)
			EXPECTED[(row * 16 + y) * WIDTH + column * 8 + x] =
				bits & (128u >> x) ? 0x00c0c0c0u : 0;
	}
	column++;
	if (column == WIDTH / 8)
		next_row();
}

static void emit(unsigned int c)
{
	reference(c);
	fbtext_putc((char)c);
}

static void check(const char *phase)
{
	unsigned int i;
	unsigned int hash = 2166136261u;

	for (i = 0; i < PIXELS; i++) {
		unsigned int pixel = FB[i];

		if (pixel != EXPECTED[i])
			failures++;
		hash = (hash ^ pixel) * 16777619u;
	}
	if (FB[-1] != SENTINEL || FB[PIXELS] != SENTINEL)
		failures++;
	puts_uart(phase);
	hex_uart(hash);
	puts_uart("\n");
}

void test_main(void)
{
	unsigned int i;

	FB[-1] = SENTINEL;
	FB[PIXELS] = SENTINEL;
	for (i = 0; i < PIXELS; i++) {
		FB[i] = SENTINEL;
		EXPECTED[i] = SENTINEL;
	}
	for (i = 0; i < 256; i++)
		emit(i);
	check("bytes ");

	for (i = 0; i < 4107; i++)
		emit(32 + i % 95);
	check("wrap ");

	for (i = 0; i < 40; i++) {
		emit(10);
		emit(13);
		emit(127);
		emit(255);
		emit('A' + i % 26);
	}
	check("controls ");

	for (i = 0; i < PIXELS; i++)
		CAPTURE[i] = FB[i];
	puts_uart(failures ? "GLYPHS FAIL\n" : "GLYPHS PASS\n");
}
