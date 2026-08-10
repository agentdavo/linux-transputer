// SPDX-License-Identifier: GPL-2.0
/*
 * Early printk support for Transputer
 * Provides console output during early kernel boot via IServer Link 0
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/serial_core.h>
#include <stdarg.h>
#include <asm/iserver.h>
#include <asm/uart.h>

/* Early console write function */
static void early_write(struct console *con, const char *s, unsigned int n)
{
	unsigned int i;

	/* Use memory-mapped UART at 0x10000000 */
	for (i = 0; i < n; i++) {
		if (s[i] == '\n')
			uart_putc('\r');
		uart_putc(s[i]);
	}
}

/* Early console structure */
static struct console tr_early_console = {
	.name	= "earlycon",
	.write	= early_write,
	.flags	= CON_PRINTBUFFER | CON_BOOT,
	.index	= -1,
};

/*
 * setup_early_printk - Initialize early console for boot messages
 * @buf: Optional parameter string (not used on Transputer)
 *
 * This function registers the early console that outputs via UART/IServer
 * protocol before the full console driver is available.
 */
int __init setup_early_printk(char *buf)
{
	/*
	 * SPINE/CHDL: register_console() early has hung/corrupted state
	 * (Iptr jumped to low memory after marker C).  Prefer raw UART for
	 * early banners; full ttyS console still registers later via
	 * transputer_console_init.  Keep register_console for T4 when
	 * not on SPINE chip profile.
	 */
	uart_putc_raw('e');
#ifdef CONFIG_TRANSPUTER_SPINE_CHIP
	/* Direct path only — avoid printk console list walks this early */
	(void)buf;
	(void)tr_early_console;
#else
	register_console(&tr_early_console);
#endif
	uart_putc_raw('E');
	return 0;
}

noinline __used int trace_vprintk_callsite(const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vprintk(fmt, ap);
	va_end(ap);

	return ret;
}

/* Allow early_param to configure early printk from command line */
early_param("earlyprintk", setup_early_printk);

/*
 * transputer_early_printk - Direct UART printk bypass
 *
 * Formats a message and writes it directly to UART,
 * completely bypassing the console subsystem (no mutex, SRCU, etc.)
 * This is the simplest way to get printk-like output during early boot.
 */
static char __tp_printk_buf[256];

/*
 * transputer_uart_puts - Write a string directly to UART
 *
 * Simple character-by-character output. No formatting.
 * This avoids vsnprintf which TCC miscompiles.
 */
static unsigned int tp_load_u8_wordpath(const char *p)
{
	unsigned int addr = (unsigned int)p;
	unsigned int aligned = addr & ~3U;
	unsigned int word = *(const unsigned int *)aligned;

	switch (addr & 3U) {
	case 0:
		return word & 0xFFU;
	case 1:
		return (word >> 8) & 0xFFU;
	case 2:
		return (word >> 16) & 0xFFU;
	default:
		return (word >> 24) & 0xFFU;
	}
}

void noinline transputer_uart_puts(const char *s)
{
	unsigned int ch;

	if (!s)
		return;

	for (;;) {
		ch = tp_load_u8_wordpath(s);
		if (ch == 0)
			break;
		if (ch == '\n')
			uart_putc_raw('\r');
		uart_putc_raw(ch);
		s++;
	}
}
