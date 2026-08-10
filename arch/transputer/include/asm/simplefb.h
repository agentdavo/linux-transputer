/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SIMPLEFB_H
#define _ASM_TRANSPUTER_SIMPLEFB_H

/*
 * Display + compositor carveouts (firmware/userspace owned, not pageable).
 *
 * Two profiles (Kconfig TRANSPUTER_SPINE_CHIP):
 *
 * T4 / TransputerOS (default):
 *   0x81a00000  2 MiB  present scratch
 *   0x81c00000  2 MiB  FBWM retained-surface SHM
 *   0x81e00000  2 MiB  simplefb XRGB8888 800×600
 *
 * SPINE / GF180MCU chip (CONFIG_TRANSPUTER_SPINE_CHIP):
 *   UART          0x10000000  (asm/uart.h — same as T4)
 *   PPU CSR       0x10001000  (not used by simplefb; scanout is RAM)
 *   FB aperture   0x81000000  2 MiB XRGB8888 800×600 (matches chip demo window)
 *   compose/scratch above FB within 128 MiB DRAM
 *
 * Chip ppu-simplefb.dtsi (OF/simpledrm) is not used: arch has no DT and
 * DRM_SIMPLEDRM depends on MMU. Detection path is platform simple-framebuffer
 * + CONFIG_FB_SIMPLE (fbdev). See arch/transputer/kernel/simplefb.c.
 */

#ifdef CONFIG_TRANSPUTER_SPINE_CHIP

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

#else /* T4 */

#define TRANSPUTER_SCRATCH_BASE		0x81a00000UL
#define TRANSPUTER_SCRATCH_SIZE		0x00200000UL
#define TRANSPUTER_COMPOSE_BASE		0x81c00000UL
#define TRANSPUTER_COMPOSE_SIZE		0x00200000UL
#define TRANSPUTER_FB_BASE		0x81e00000UL
#define TRANSPUTER_FB_CARVEOUT_SIZE	0x00200000UL
#define TRANSPUTER_FB_WIDTH		800
#define TRANSPUTER_FB_HEIGHT		600
#define TRANSPUTER_FB_STRIDE		(TRANSPUTER_FB_WIDTH * 4)
#define TRANSPUTER_FB_SIZE		(TRANSPUTER_FB_STRIDE * TRANSPUTER_FB_HEIGHT)
#define TRANSPUTER_FB_FORMAT		"x8r8g8b8"

#endif

#endif /* _ASM_TRANSPUTER_SIMPLEFB_H */
