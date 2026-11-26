/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SIMPLEFB_H
#define _ASM_TRANSPUTER_SIMPLEFB_H

/*
 * Unified display + compositor carveouts (firmware/userspace owned,
 * not pageable). Shared identically by both the RTL platform and the
 * software emulator, fitting within CONFIG_TRANSPUTER_RAM_MIB >= 24:
 *
 *   UART0         0x10000000  Console UART (asm/uart.h)
 *   UART1         0x11000000  Keyboard UART
 *   UART2         0x12000000  Mouse UART
 *   SD MMIO       0x20000000  128 MiB SD card (/dev/tpsd, /dev/tpsd1)
 *   NET MMIO      0x30000000  Ethernet NIC (tpnet)
 *   FB aperture   0x81000000  2 MiB XRGB8888 800x600
 *   compose SHM   0x81200000  2 MiB retained-surface compositor SHM
 *   scratch       0x81400000  2 MiB present scratch
 *   display ctrl  0xA0000000  (0xA0000014 FLIP)
 *   reserve box:  0x81000000 .. 0x81600000  (+16..+22 MiB)
 *
 * Detection path is platform simple-framebuffer + CONFIG_FB_SIMPLE (fbdev).
 * See arch/transputer/kernel/simplefb.c.
 */

#define TRANSPUTER_FB_BASE		0x81000000UL
#define TRANSPUTER_FB_CARVEOUT_SIZE	0x00200000UL
#define TRANSPUTER_COMPOSE_BASE		0x81200000UL
#define TRANSPUTER_COMPOSE_SIZE		0x00200000UL
#define TRANSPUTER_SCRATCH_BASE		0x81400000UL
#define TRANSPUTER_SCRATCH_SIZE		0x00200000UL
#define TRANSPUTER_FB_WIDTH		800
#define TRANSPUTER_FB_HEIGHT		600
#define TRANSPUTER_FB_STRIDE		(TRANSPUTER_FB_WIDTH * 4)
#define TRANSPUTER_FB_SIZE		(TRANSPUTER_FB_STRIDE * TRANSPUTER_FB_HEIGHT)
#define TRANSPUTER_FB_FORMAT		"x8r8g8b8"

/* Single reserved span covering FB + compose + scratch (0x81000000..0x81600000). */
#define TRANSPUTER_DISP_RESERVE_BASE	TRANSPUTER_FB_BASE
#define TRANSPUTER_DISP_RESERVE_END	(TRANSPUTER_SCRATCH_BASE + \
					 TRANSPUTER_SCRATCH_SIZE)

#if defined(CONFIG_TRANSPUTER_RAM_MIB)
#if (TRANSPUTER_DISP_RESERVE_END - 0x80000000) > \
	(CONFIG_TRANSPUTER_RAM_MIB * 0x100000)
#error "display carve exceeds CONFIG_TRANSPUTER_RAM_MIB — do not raise RAM; shrink the carve"
#endif
#endif

#endif /* _ASM_TRANSPUTER_SIMPLEFB_H */
