// SPDX-License-Identifier: GPL-2.0
/*
 * Fixed firmware framebuffer description for TransputerOS.
 *
 * The display hardware is deliberately register-free: an always-on scanout
 * engine consumes this fixed XRGB8888 aperture.  Linux's standard simplefb
 * driver owns /dev/fb0; no architecture-private fbdev driver is involved.
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>
#include <asm/simplefb.h>

static struct simplefb_platform_data transputer_simplefb_pdata = {
	.width = TRANSPUTER_FB_WIDTH,
	.height = TRANSPUTER_FB_HEIGHT,
	.stride = TRANSPUTER_FB_STRIDE,
	.format = "x8r8g8b8",
};

static struct resource transputer_simplefb_resource = {
	.start = TRANSPUTER_FB_BASE,
	.end = TRANSPUTER_FB_BASE + TRANSPUTER_FB_SIZE - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device transputer_simplefb_device = {
	.name = "simple-framebuffer",
	.id = 0,
	.num_resources = 1,
	.resource = &transputer_simplefb_resource,
	.dev = {
		.platform_data = &transputer_simplefb_pdata,
	},
};

static int __init transputer_simplefb_init(void)
{
	int ret;

	ret = platform_device_register(&transputer_simplefb_device);
	if (ret)
		pr_err("Transputer: simplefb registration failed: %d\n", ret);
	else
		pr_info("Transputer: simplefb registered at %pa (%ux%u, stride %u)\n",
			&transputer_simplefb_resource.start,
			transputer_simplefb_pdata.width,
			transputer_simplefb_pdata.height,
			transputer_simplefb_pdata.stride);

	return ret;
}
arch_initcall(transputer_simplefb_init);
