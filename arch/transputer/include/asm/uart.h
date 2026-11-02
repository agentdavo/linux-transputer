/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer memory-mapped UART helpers
 *
 * IMPORTANT: uart_putc_raw / uart_debug_byte / uart_puthex32 are macros,
 * not extern functions.  The TCC linker has a relocation bug that resolves
 * cross-file function calls to wrong addresses in large binaries.  Using
 * macros ensures the volatile store is emitted inline at each call site.
 */

#ifndef __ASM_TRANSPUTER_UART_H
#define __ASM_TRANSPUTER_UART_H

#include <linux/types.h>

/* T4 emulator + SPINE DEVICE uart0 (A[31:28]==0x1, APB slot 0). Keep in sync
 * with chip ip/uart + ip/bus README; no DTSI override on this arch yet. */
#define UART_MMIO_ADDR 0x10000000u

/* Inline UART write — avoids cross-file call relocation bugs in TCC linker */
#define uart_putc_raw(ch) \
	do { *(volatile unsigned int *)UART_MMIO_ADDR = (unsigned int)(ch); } while (0)

#define uart_debug_byte() \
	do { *(volatile unsigned int *)UART_MMIO_ADDR = (unsigned int)'D'; } while (0)

void uart_putc(char c);
void uart_puts(const char *str);
void uart_puthex32(unsigned int val);

/* Inline hex output macro — avoids cross-file call */
#define uart_debug_hex32(v) do { \
	unsigned int _v = (unsigned int)(v); \
	int _i; \
	for (_i = 28; _i >= 0; _i -= 4) { \
		unsigned int _n = (_v >> _i) & 0xF; \
		uart_putc_raw(_n < 10 ? '0' + _n : 'a' + _n - 10); \
	} \
} while (0)

#endif /* __ASM_TRANSPUTER_UART_H */

