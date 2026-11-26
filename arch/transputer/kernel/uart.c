/*
 * Simple memory-mapped UART driver for Transputer
 *
 * Memory map:
 *   0x10000000 - UART_DATA (read/write data register)
 *   0x10000004 - UART_STATUS (read-only status register)
 *
 * NOTE: uart_putc_raw() and uart_debug_byte() are macros defined in
 * <asm/uart.h> to avoid TCC linker cross-file relocation bugs.
 * This file provides the higher-level functions (uart_putc, uart_puts)
 * that use those macros internally.
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#include <asm/uart.h>

/* Print a 32-bit value as 8 hex digits */
void uart_puthex32(unsigned int val)
{
	int i;
	for (i = 28; i >= 0; i -= 4) {
		unsigned int nibble = (val >> i) & 0xF;
		unsigned int ch = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
		uart_putc_raw(ch);
	}
}

void uart_putc(char c)
{
	uart_putc_raw((unsigned int)c);
}

void uart_puts(const char *s)
{
	while (*s) {
		if (*s == '\n')
			uart_putc_raw('\r');
		uart_putc_raw((unsigned int)(unsigned char)*s);
		s++;
	}
}

/* Console output function */
static void transputer_uart_console_write(struct console *co, const char *s,
					  unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		if (s[i] == '\n')
			uart_putc_raw('\r');
		uart_putc_raw((unsigned int)(unsigned char)s[i]);
	}
}

static struct console transputer_uart_console = {
	.name	= "ttyS",
	.write	= transputer_uart_console_write,
	.flags	= CON_PRINTBUFFER | CON_BOOT,
	.index	= -1,
};

static int __init transputer_uart_console_init(void)
{
	register_console(&transputer_uart_console);

	uart_puts("\n\n");
	uart_puts("========================================\n");
	uart_puts(" Transputer Linux Kernel Console\n");
	uart_puts(" UART Driver Initialized at 0x10000000\n");
	uart_puts("========================================\n");
	uart_puts("\n");

	return 0;
}
console_initcall(transputer_uart_console_init);

/* Early console support (before full console infrastructure) */
void __init early_uart_write(const char *s, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		if (s[i] == '\n')
			uart_putc_raw('\r');
		uart_putc_raw((unsigned int)(unsigned char)s[i]);
	}
}
