// SPDX-License-Identifier: GPL-2.0
/*
 * Fixed firmware framebuffer description for TransputerOS / SPINE.
 *
 * Registers a platform_device named "simple-framebuffer" with
 * simplefb_platform_data.  CONFIG_FB_SIMPLE binds drivers/video/fbdev/simplefb.c
 * and creates /dev/fb0.
 *
 * DRM simpledrm (DRM_SIMPLEDRM) also matches name "simple-framebuffer" and
 * platform_data, but upstream depends on MMU + GEM shmem — not available on
 * this NOMMU port.  We stay on fbdev simplefb and log that clearly.
 */

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>
#include <asm/simplefb.h>
#include <asm/uart.h>

static struct simplefb_platform_data transputer_simplefb_pdata = {
	.width  = TRANSPUTER_FB_WIDTH,
	.height = TRANSPUTER_FB_HEIGHT,
	.stride = TRANSPUTER_FB_STRIDE,
	.format = TRANSPUTER_FB_FORMAT,
};

static struct resource transputer_simplefb_resource = {
	.start = TRANSPUTER_FB_BASE,
	.end   = TRANSPUTER_FB_BASE + TRANSPUTER_FB_SIZE - 1,
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

	/* Early marker so SPINE sim UART shows FB path even if printk is late */
	uart_putc_raw('f');

	ret = platform_device_register(&transputer_simplefb_device);
	if (ret) {
		uart_putc_raw('!');
		pr_err("Transputer: simplefb registration failed: %d\n", ret);
		return ret;
	}

	uart_putc_raw('F'); /* simplefb platform device registered */
	pr_info("Transputer: simplefb registered at 0x%08lx (%ux%u stride %u fmt %s)\n",
		(unsigned long)TRANSPUTER_FB_BASE,
		transputer_simplefb_pdata.width,
		transputer_simplefb_pdata.height,
		transputer_simplefb_pdata.stride,
		transputer_simplefb_pdata.format);
#ifdef CONFIG_TRANSPUTER_SPINE_CHIP
	pr_info("Transputer: SPINE profile — FB_SIMPLE only (simpledrm needs MMU)\n");
#else
	pr_info("Transputer: T4 profile — FB_SIMPLE (simpledrm needs MMU)\n");
#endif
	return 0;
}
arch_initcall(transputer_simplefb_init);
