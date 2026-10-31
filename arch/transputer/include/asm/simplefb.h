/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SIMPLEFB_H
#define _ASM_TRANSPUTER_SIMPLEFB_H

/*
 * Display + compositor carveouts (firmware/userspace owned, not pageable).
 *
 * Both profiles fit in CONFIG_TRANSPUTER_RAM_MIB=32. Do not raise RAM to
 * 128 MiB to make the FB easier.
 *
 * T4 / TransputerOS (!TRANSPUTER_SPINE_CHIP):
 *   0x81a00000  2 MiB  present scratch
 *   0x81c00000  2 MiB  FBWM retained-surface SHM
 *   0x81e00000  2 MiB  simplefb XRGB8888 800×600
 *   reserve box: 0x81a00000 .. 0x82000000 (top 6 MiB of the 32 MiB window)
 *
 * SPINE / GF180MCU chip (CONFIG_TRANSPUTER_SPINE_CHIP), any RAM_MIB ≥ 22:
 *   UART          0x10000000  (asm/uart.h — same as T4)
 *   PPU CSR       0x10001000  (not used by simplefb; scanout is RAM)
 *   FB aperture   0x81000000  2 MiB XRGB8888 800×600 (chip PPU demo window)
 *   compose       0x81200000  2 MiB
 *   scratch       0x81400000  2 MiB
 *   reserve box:  0x81000000 .. 0x81600000  (+16..+22 MiB)
 *
 * Chip ppu-simplefb.dtsi (OF/simpledrm) is not used: arch has no DT and
 * DRM_SIMPLEDRM depends on MMU. Detection path is platform simple-framebuffer
 * + CONFIG_FB_SIMPLE (fbdev). See arch/transputer/kernel/simplefb.c.
 * kernel/fb.c is the T4 VGA text console and is not linked.
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

/*
 * Single reserved span covering FB + compose + scratch.  Address order
 * differs: SPINE is FB < compose < scratch; T4 is scratch < compose < FB.
 * setup.c must reserve this box, not "FB_BASE .. SCRATCH_END".
 */
#ifdef CONFIG_TRANSPUTER_SPINE_CHIP
#define TRANSPUTER_DISP_RESERVE_BASE	TRANSPUTER_FB_BASE
#define TRANSPUTER_DISP_RESERVE_END	(TRANSPUTER_SCRATCH_BASE + \
					 TRANSPUTER_SCRATCH_SIZE)
#else
#define TRANSPUTER_DISP_RESERVE_BASE	TRANSPUTER_SCRATCH_BASE
#define TRANSPUTER_DISP_RESERVE_END	(TRANSPUTER_FB_BASE + \
					 TRANSPUTER_FB_CARVEOUT_SIZE)
#endif

#if defined(CONFIG_TRANSPUTER_RAM_MIB)
#if (TRANSPUTER_DISP_RESERVE_END - 0x80000000) > \
	(CONFIG_TRANSPUTER_RAM_MIB * 0x100000)
#error "display carve exceeds CONFIG_TRANSPUTER_RAM_MIB — do not raise RAM; shrink the carve"
#endif
#endif

#endif /* _ASM_TRANSPUTER_SIMPLEFB_H */
