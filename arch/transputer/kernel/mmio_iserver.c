// SPDX-License-Identifier: GPL-2.0
/*
 * Memory-mapped IServer implementation for Transputer
 *
 * Uses direct memory-mapped I/O to Link 0 instead of 'out' instruction
 * to work around T4 emulator bugs with channel I/O instructions.
 *
 * Based on working MINIX example: hello-iserver.c
 */

#include <linux/init.h>
#include <asm/uart.h>

static void mmio_iserver_write(const char *str, unsigned int len)
{
    unsigned int i;

    for (i = 0; i < len; i++) {
        if (str[i] == '\n')
            uart_putc('\r');
        uart_putc(str[i]);
    }
}

/*
 * IServer putc - output a single character
 *
 * This provides the same interface as iserver.S:iserver_putc()
 * but uses memory-mapped I/O instead of 'out' instruction.
 */
void iserver_putc(char c)
{
    uart_putc(c);
}

/*
 * IServer puts - output a string
 * For early console and debug output
 */
void iserver_puts(const char *str)
{
    unsigned int len = 0;

    /* Use for-loop to avoid TCC while(*p++) miscompilation bug */
    for (len = 0; str[len] != '\0'; len++)
        ;
    mmio_iserver_write(str, len);
}

/*
 * Early initialization message
 * Called before console framework is ready
 */
void __init mmio_iserver_early_init(void)
{
    iserver_puts("Transputer UART console initialized\n");
}
