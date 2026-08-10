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
#include <asm/thds.h>
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

	/* Set up command line — byte loop (avoid sized_strscpy / TCC edge cases) */
	{
		static const char cmd[] =
			"earlyprintk console=ttyS0 ignore_loglevel "
			"ihash_entries=256 dhash_entries=256 thash_entries=256 "
			"uhash_entries=256 rhash_entries=256 rdinit=/init "
			"t9k_enforce t9k_hard_p";
		unsigned int i;

		uart_putc_raw('1');
		*cmdline_p = (char *)cmd;
		uart_putc_raw('2');
		for (i = 0; i < COMMAND_LINE_SIZE - 1 && cmd[i]; i++)
			boot_command_line[i] = cmd[i];
		boot_command_line[i] = '\0';
		uart_putc_raw('3');
	}
	uart_putc_raw('C');

	/* Set up early console for linux_banner */
	setup_early_printk(NULL);
	uart_putc_raw('D');

	/*
	 * L-process THDS (optional): leave G null-THDS before this so head
	 * markers stay G-safe; install real THDS so later overflow/illegal
	 * L-trap instead of G-halt (ISM §10.1.3).
	 */
	/* Markers live inside the helper (`t`/`s`/`h`/`L` or `!`+hex).
	 * Do not branch on the C return value: after uart_putc_raw the
	 * AReg residue can make `== 0` look false even when selth stuck. */
	(void)t9k_thds_boot_install();

	/*
	 * === Memory initialization (modern NOMMU/memblock pattern) ===
	 *
	 * Transputer memory layout:
	 *   T4 default: CONFIG_TRANSPUTER_RAM_MIB=32, FB @ 0x81e00000
	 *   SPINE chip: CONFIG_TRANSPUTER_SPINE_CHIP, RAM 128, FB @ 0x81000000
	 *   0x80000000  MemStart (MinInt)
	 *   0x80000070  _stext (kernel code start, ELF entry)
	 *   ...         free memory to memory_end
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
	/* Debug: dump memory_start/end high nibbles if end looks wrong */
	if (memory_start >= memory_end || memory_start < 0x80000000UL) {
		uart_putc_raw('!');
		uart_putc_raw('m'); /* bad memory bounds */
	}

	/*
	 * TCC / section init can leave memblock.memory.cnt or total_size
	 * non-zero while regions[0].size is still 0 — that trips WARN_ON in
	 * memblock_add_range and has hung early boot (trail …EEww, never '1').
	 * Force a clean empty type before the first add.
	 */
	{
		extern struct memblock memblock;
		memblock.memory.cnt = 0;
		memblock.memory.total_size = 0;
		if (memblock.memory.regions)
			memblock.memory.regions[0].size = 0;
		memblock.reserved.cnt = 0;
		memblock.reserved.total_size = 0;
		if (memblock.reserved.regions)
			memblock.reserved.regions[0].size = 0;
	}
	uart_putc_raw('0'); /* memblock scrubbed */

	/*
	 * Describe only allocatable RAM to memblock.  Excluding the kernel image
	 * keeps ARCH_PFN_OFFSET at the first page represented by FLATMEM, which is
	 * the same high-address invariant used by the working 2.6 port.
	 *
	 * Direct first-region install: memblock_add_range() has hung/WARN'd on
	 * this port after E (trail …E0w, never '1').  One empty→single region
	 * is trivial — do it by hand.
	 */
	{
		extern struct memblock memblock;
		struct memblock_region *r = &memblock.memory.regions[0];
		phys_addr_t sz = (phys_addr_t)(memory_end - memory_start);

		r->base = (phys_addr_t)memory_start;
		r->size = sz;
		r->flags = 0;
		memblock.memory.cnt = 1;
		memblock.memory.total_size = sz;
	}
	uart_putc_raw('1'); /* after memory region install */
	/*
	 * FB / compose / scratch carveouts — install reserved regions by hand
	 * (same TCC/memblock_add_range hang as memory).  Adjacent 2MiB blocks
	 * at 0x81000000.. merge into one reserved range.
	 */
	{
		extern struct memblock memblock;
		struct memblock_region *r = &memblock.reserved.regions[0];
		phys_addr_t rbase = (phys_addr_t)TRANSPUTER_FB_BASE;
		phys_addr_t rend = (phys_addr_t)(TRANSPUTER_SCRATCH_BASE +
						 TRANSPUTER_SCRATCH_SIZE);
		phys_addr_t rsz;

		if (rbase < (phys_addr_t)memory_start)
			rbase = (phys_addr_t)memory_start;
		if (rend > (phys_addr_t)memory_end)
			rend = (phys_addr_t)memory_end;
		rsz = (rend > rbase) ? (rend - rbase) : 0;
		if (rsz) {
			r->base = rbase;
			r->size = rsz;
			r->flags = 0;
			memblock.reserved.cnt = 1;
			memblock.reserved.total_size = rsz;
		}
	}
	uart_putc_raw('2');
	uart_putc_raw('3');
	uart_putc_raw('4');
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
	uart_putc_raw('Q');

	/*
	 * Publish after BSS/data initialization and before the first userspace
	 * task.  bFLT binaries use this fixed NOMMU gateway for VFS syscalls.
	 */
	tp_publish_syscall_gate();
	uart_putc_raw('R'); /* setup_arch complete */
}
