// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal console + TTY driver for Transputer Linux kernel with TinyCC
 *
 * Uses memory-mapped UART at 0x10000000 for console output.
 * Registers a tty_driver so sys_open("/dev/console") works.
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/serial_core.h>
#include <asm/uart.h>
#include <linux/string.h>

/* ---- printk console ---- */

static void tr_console_write(struct console *con, const char *s, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (s[i] == '\n')
			uart_putc('\r');
		uart_putc(s[i]);
	}
}

static struct tty_driver *tr_console_device(struct console *c, int *index);

struct console transputer_console = {
    .name = "ttyS",
    .write = tr_console_write,
    .device = tr_console_device,
    .flags = CON_PRINTBUFFER | CON_ENABLED | CON_CONSDEV,
    .index = 0,
};

int __init transputer_console_init(void)
{
	register_console(&transputer_console);
	return 0;
}

/* ---- TTY driver (makes /dev/console openable) ---- */

static struct tty_driver *uart_tty_driver;

static int uart_tty_open(struct tty_struct *tty, struct file *filp)
{
	return 0;
}

static void uart_tty_close(struct tty_struct *tty, struct file *filp)
{
}

static int uart_tty_write(struct tty_struct *tty,
			  const unsigned char *buf, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		if (buf[i] == '\n')
			uart_putc('\r');
		uart_putc(buf[i]);
	}
	return count;
}

static int uart_tty_write_room(struct tty_struct *tty)
{
	return 4096;
}

static struct tty_operations uart_tty_ops = {
	.open = uart_tty_open,
	.close = uart_tty_close,
	.write = uart_tty_write,
	.write_room = uart_tty_write_room,
};

static struct tty_driver *tr_console_device(struct console *c, int *index)
{
	*index = 0;
	return uart_tty_driver;
}

static int __init uart_tty_init(void)
{
	int ret;

	uart_tty_driver = tty_alloc_driver(1, TTY_DRIVER_REAL_RAW);
	if (IS_ERR(uart_tty_driver))
		return PTR_ERR(uart_tty_driver);

	uart_tty_driver->driver_name = "transputer_uart";
	uart_tty_driver->name = "ttyS";
	uart_tty_driver->major = 4;
	uart_tty_driver->minor_start = 64;
	uart_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	uart_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	uart_tty_driver->init_termios = tty_std_termios;
	uart_tty_driver->flags = TTY_DRIVER_REAL_RAW;

	tty_set_operations(uart_tty_driver, &uart_tty_ops);

	ret = tty_register_driver(uart_tty_driver);
	if (ret) {
		tty_driver_kref_put(uart_tty_driver);
		uart_tty_driver = NULL;
		return ret;
	}

	return 0;
}
device_initcall(uart_tty_init);
