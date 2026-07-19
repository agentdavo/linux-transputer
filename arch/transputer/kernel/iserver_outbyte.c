// SPDX-License-Identifier: GPL-2.0
/*
 * IServer implementation using outbyte instruction for Transputer
 *
 * Uses the 'outbyte' instruction instead of memory-mapped I/O
 * to properly communicate with the T4 emulator's IServer.
 */

#include <linux/init.h>

/* T4 emulator Link 0 address */
#define LINK0_OUT 0x80000000

/* IServer protocol commands */
#define SP_PUTS     0x0F

/* IServer stream IDs */
#define SP_STDOUT   1

/*
 * Send one byte via outbyte instruction
 * outbyte uses: AReg=byte value, BReg=channel address
 *
 * The outbyte instruction (0x0a) sends the byte at WPtr to channel BReg.
 * So we need to: store byte at WPtr, load channel, execute outbyte
 */
static inline void send_byte_to_link0(unsigned char byte)
{
	/* For TCC inline assembly, we use .byte directives */
	/* We need to generate:
	 *   - Store byte in workspace
	 *   - Load channel address 0x80000000 into BReg
	 *   - Execute outbyte (opcode 0x0a)
	 *
	 * Transputer opcodes:
	 *   ldc N (0-15): 0x40 + N
	 *   stl N: 0xD0 + N (for N=0-15)
	 *   pfix N: 0x20 + N (prefix for building large constants)
	 *   outbyte: 0x0a
	 */

	/* This is tricky - we need to build 0x80000000 using prefix bytes */
	/* 0x80000000 = pfix 8, pfix 0, pfix 0, pfix 0, ldc 0 */

	__asm__ volatile(
		/* Load byte value into AReg and store at WPtr[0] */
		".byte 0x40 | (%0 & 0xF)\n\t"      /* ldc (byte & 0xF) for values 0-15 */
		".byte 0xD0\n\t"                    /* stl 0 - store at WPtr[0] */

		/* Build 0x80000000 in AReg using prefix bytes */
		".byte 0x28\n\t"                    /* pfix 8 */
		".byte 0x20\n\t"                    /* pfix 0 */
		".byte 0x20\n\t"                    /* pfix 0 */
		".byte 0x20\n\t"                    /* pfix 0 */
		".byte 0x40\n\t"                    /* ldc 0 -> AReg = 0x80000000 */

		".byte 0x0A\n\t"                    /* outbyte */
		: /* no outputs */
		: "i"(byte)  /* byte must be a compile-time constant */
		: "memory"
	);
}

/*
 * Send IServer SP_PUTS command
 */
static void iserver_puts_outbyte(const char *str, unsigned int len)
{
	unsigned int i;

	/* We can't use the inline assembly approach above because the bytes
	 * aren't compile-time constants. We need a different approach.
	 *
	 * Instead, let's write the bytes to workspace and use a loop with
	 * outbyte for each one.
	 */

	/* For now, fall back to a simpler implementation that works */
	/* This is a placeholder - we need to implement proper outbyte */
}

void iserver_putc(char c)
{
	/* TODO: Implement using outbyte */
}

void iserver_puts(const char *str)
{
	/* TODO: Implement using outbyte */
}

void __init iserver_outbyte_early_init(void)
{
	/* TODO: Send initial message using outbyte */
}
