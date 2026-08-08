// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer architecture setup
 * Memory discovery follows the current memblock-based NOMMU pattern.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/memblock.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/simplefb.h>
#include <asm/uart.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

/* External symbols from linker script - TCC can't resolve these, so they're all 0.
 * Use __linker_* surrogates from head.S, patched by transputer-link.c */
extern char _stext[], _etext[], _edata[], _end[];

/* Linker symbol surrogates - patched data words from head.S */
extern unsigned long __linker_stext, __linker_etext, __linker_edata, __linker_end;
unsigned long __fp_tp_vfs_caches_init = 0xDEAD0020;

/* Memory bounds (also used by mmu.c for paging_init/mem_init) */
extern unsigned long memory_start;
extern unsigned long memory_end;

/* Boot parameters from head.S */
extern unsigned long boot_params[8];

/* Forward declaration for early console setup */
extern int setup_early_printk(char *buf);

/* Console from console.c */
extern struct console transputer_console;

/* Untouched command line buffer from init/main.c */
extern char boot_command_line[COMMAND_LINE_SIZE];

/* From mmu.c */
extern void paging_init(void);

/* Fixed NOMMU userspace-to-kernel syscall gateway from syscall.c. */
extern void tp_publish_syscall_gate(void);

void __init setup_arch(char **cmdline_p)
{
	uart_putc_raw('A');

	/* TRANSPUTER WORKAROUND: Force console_drivers to NULL.
	 * Despite BSS being zeroed in head.S, some data relocation
	 * or initialization code writes a garbage value to this address.
	 * Without this, register_console() follows a corrupted linked list. */
	{
		extern struct console *console_drivers;
		extern int preferred_console;
		extern int selected_console;
		/* Use volatile to prevent any optimization */
		*(volatile unsigned long *)&console_drivers = 0;
		preferred_console = -1;
		selected_console = -1;
	}
	uart_putc_raw('B');

	/* Set up command line */
	/*
	 * t9k_enforce  — soft-mm ACTIVE (quarter-identity access_ok)
	 * t9k_hard_p   — enter user via goprot (P-process); implies enforce
	 */
	/*
	 * t9k_enforce  — soft-mm ACTIVE (quarter-identity access_ok)
	 * t9k_hard_p   — enter user via goprot (implies enforce)
	 */
	*cmdline_p = "earlyprintk console=ttyS0 ignore_loglevel "
		     "ihash_entries=256 dhash_entries=256 thash_entries=256 "
		     "uhash_entries=256 rhash_entries=256 rdinit=/init "
		     "t9k_enforce t9k_hard_p";
	boot_command_line[0] = '\0';
	strscpy(boot_command_line, *cmdline_p, COMMAND_LINE_SIZE);
	uart_putc_raw('C');

	/* Set up early console for linux_banner */
	setup_early_printk(NULL);
	uart_putc_raw('D');

	/*
	 * === Memory initialization (modern NOMMU/memblock pattern) ===
	 *
	 * Transputer memory layout (tpos_defconfig uses -sm 25 = 32MiB):
	 *   0x80000000  MemStart (MinInt)
	 *   0x80000070  _stext (kernel code start, ELF entry)
	 *   ...         kernel code + data + bss
	 *   _end        end of kernel image
	 *   ...         free memory
	 *   memory_end  MemStart + CONFIG_TRANSPUTER_RAM_MIB
	 *
	 * PAGE_OFFSET = 0, so PFN = addr >> PAGE_SHIFT directly.
	 */

	/* Determine memory bounds - use __linker_end instead of _end
	 * (TCC can't resolve linker script "= .;" symbol assignments) */
	memory_start = PAGE_ALIGN(__linker_end);
	/* Keep this matched to T4's -sm value. A configured size also prevents
	 * early boot from initialising pages that the emulator does not own. */
	memory_end = 0x80000000UL +
		(CONFIG_TRANSPUTER_RAM_MIB * 1024UL * 1024UL);
	/* A kernel image (e.g. with an embedded WAD initramfs) larger than
	 * the configured RAM makes end - start wrap negative-unsigned, and
	 * memblock then believes it owns ~2 GiB — the percpu allocator
	 * "stall at mem auto-init, 515304 pages" failure.  Refuse loudly. */
	if (memory_start >= memory_end)
		panic("kernel image ends above CONFIG_TRANSPUTER_RAM_MIB=%d MiB — raise it (and t4 -sm)",
		      CONFIG_TRANSPUTER_RAM_MIB);

	/* Set up init_mm structure - use __linker_* surrogates */
	init_mm.start_code = __linker_stext;
	init_mm.end_code = __linker_etext;
	init_mm.end_data = __linker_edata;
	init_mm.brk = (unsigned long)0;
	uart_putc_raw('E');

	/*
	 * Describe only allocatable RAM to memblock.  Excluding the kernel image
	 * keeps ARCH_PFN_OFFSET at the first page represented by FLATMEM, which is
	 * the same high-address invariant used by the working 2.6 port.
	 */
	memblock_add(memory_start, memory_end - memory_start);
	/* The always-on display DMA scans this firmware-owned RAM surface.
	 * Keep the full 2 MiB aligned carveout away from the page allocator. */
	memblock_reserve(TRANSPUTER_FB_BASE, TRANSPUTER_FB_CARVEOUT_SIZE);
	/* Retained-surface compositor SHM used by FBWM and guest GUI apps. */
	memblock_reserve(TRANSPUTER_COMPOSE_BASE, TRANSPUTER_COMPOSE_SIZE);
	/* Atomic present scratch: compose off-aperture then copy once. */
	memblock_reserve(TRANSPUTER_SCRATCH_BASE, TRANSPUTER_SCRATCH_SIZE);
	min_low_pfn = memory_start >> PAGE_SHIFT;
	max_low_pfn = memory_end >> PAGE_SHIFT;
	max_pfn = max_low_pfn;
	uart_putc_raw('F');

	/*
	 * Set up basic memory zones via paging_init().
	 * On NOMMU this records high_memory.  Linux 7 creates zones in
	 * mm_core_init_early(), immediately after setup_arch() returns.
	 */
	paging_init();
	uart_putc_raw('P');

	/*
	 * Skip calibrate_delay() — T4 emulator has no timer interrupt
	 * to increment jiffies, so the calibration loop hangs forever.
	 * preset_lpj is checked first in calibrate_delay().
	 */
	preset_lpj = 4096;

	/*
	 * Publish after BSS/data initialization and before the first userspace
	 * task.  bFLT binaries use this fixed NOMMU gateway for VFS syscalls.
	 */
	tp_publish_syscall_gate();
}
