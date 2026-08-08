/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_SIMPLEFB_H
#define _ASM_TRANSPUTER_SIMPLEFB_H

/* Display + compositor carveouts (firmware/userspace owned, not pageable).
 *
 *   0x81a00000  2 MiB  present scratch (atomic scanout copy)
 *   0x81c00000  2 MiB  FBWM retained-surface SHM (tpos_shm)
 *   0x81e00000  2 MiB  simplefb XRGB8888 aperture
 */
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

#endif
