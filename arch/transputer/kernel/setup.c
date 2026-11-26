// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer architecture setup
 * Memory discovery follows the current memblock-based NOMMU pattern.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/memblock.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/simplefb.h>
#include <asm/uart.h>
#include <asm/thds.h>
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
#include <asm/smp.h>
#endif
#include <linux/delay.h>
#include <linux/jiffies.h>
#ifdef CONFIG_TRANSPUTER_SMP_BOOT
#include <asm/tp_smp_boot.h>
#endif

static void *cpuinfo_start(struct seq_file *m, loff_t *pos)
{
	unsigned int cpu;

	/* Check the full loff_t before narrowing a userspace seek position. */
	if (*pos < 0 || *pos >= nr_cpu_ids)
		return NULL;
	for (cpu = (unsigned int)*pos; cpu < nr_cpu_ids; cpu++) {
		if (cpu_online(cpu)) {
			*pos = cpu;
			return (void *)(unsigned long)(cpu + 1);
		}
	}
	return NULL;
}

static void *cpuinfo_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return cpuinfo_start(m, pos);
}

static void cpuinfo_stop(struct seq_file *m, void *v)
{
}

static int cpuinfo_show(struct seq_file *m, void *v)
{
	unsigned int cpu = (unsigned long)v - 1;

	seq_printf(m, "processor\t: %u\nmodel name\t: Transputer\n\n", cpu);
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start = cpuinfo_start,
	.next = cpuinfo_next,
	.stop = cpuinfo_stop,
	.show = cpuinfo_show,
};

/* External symbols from linker script - TCC can't resolve these, so they're all 0.
 * Use __linker_* surrogates from head.S, patched by transputer-link.c */
extern char _stext[], _etext[], _edata[], _end[];

/* Linker symbol surrogates - patched data words from head.S */
extern unsigned long __linker_stext, __linker_etext, __linker_edata, __linker_end;

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

#ifdef CONFIG_MMU
#include <asm/pgm1.h>

static u64 tp_ram0_end, tp_high_base, tp_high_size;

/*
 * The first region (memory_start..memory_end, low memory) was installed by
 * hand above.  Add RAM0 beyond the window and the bank above 4 GiB, keep
 * early allocations in low memory, and reserve RAM0 addresses the legacy
 * emulator display map still decodes when RAM0 has grown over them.
 */
/* /proc/iomem: the kernel image and every RAM region memblock knows */
static struct resource tp_kernel_res = {
	.name = "Kernel image",
	.flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY,
};
static struct resource tp_ram_res[4];

static void __init tp_ram_resources(void)
{
	phys_addr_t start, end;
	u64 i;
	unsigned int n = 0;

	tp_kernel_res.start = 0x80000000UL;
	tp_kernel_res.end = memory_start - 1;
	request_resource(&iomem_resource, &tp_kernel_res);
	for_each_mem_range(i, &start, &end) {
		struct resource *r;

		if (n == ARRAY_SIZE(tp_ram_res))
			break;
		r = &tp_ram_res[n++];
		r->name = "System RAM";
		r->start = start;
		r->end = end - 1;
		r->flags = IORESOURCE_SYSTEM_RAM | IORESOURCE_BUSY;
		request_resource(&iomem_resource, r);
	}
}

static void __init tp_memory_banks(void)
{
	static const struct { u32 base, size; } legacy[] = {
		{ 0x90000000u, 0x00400000u },	/* legacy framebuffer */
		{ 0xA0000000u, 0x00010000u },	/* VGA control */
	};
	unsigned int i;

	if (tp_ram0_end > memory_end)
		memblock_add(memory_end, tp_ram0_end - memory_end);
	if (tp_high_size)
		memblock_add(tp_high_base, tp_high_size);
	for (i = 0; i < ARRAY_SIZE(legacy); i++)
		if (legacy[i].base >= memory_start &&
		    (u64)legacy[i].base + legacy[i].size <= tp_ram0_end)
			memblock_reserve(legacy[i].base, legacy[i].size);
#ifdef CONFIG_TRANSPUTER_PAGING_USER_SELFTEST
	/* The two frames 4 GiB apart that pg_user_selftest maps by PFN */
	if (tp_high_size >= (1ULL << 32) + 6 * PAGE_SIZE) {
		memblock_reserve(tp_high_base + 5 * PAGE_SIZE, PAGE_SIZE);
		memblock_reserve(tp_high_base + (1ULL << 32) + 5 * PAGE_SIZE, PAGE_SIZE);
	}
#endif
	memblock_set_current_limit(memory_end);
	max_pfn = PHYS_PFN(memblock_end_of_DRAM());
	tp_ram_resources();
	pr_info("pgm1: RAM0 %08lx..%010llx, low memory to %08lx, high bank %010llx..%010llx, "
		"max_low_pfn %lx max_pfn %lx, struct page %u bytes\n",
		memory_start, tp_ram0_end, memory_end, tp_high_base,
		tp_high_base + tp_high_size, max_low_pfn, max_pfn,
		(unsigned int)sizeof(struct page));
}
#endif

void __init setup_arch(char **cmdline_p)
{
	uart_putc_raw('A');

	uart_putc_raw('B');

	/* Set up command line — byte loop (avoid sized_strscpy / TCC edge cases) */
	{
		static const char cmd[] =
			"earlyprintk console=ttyS0 ignore_loglevel "
			"ihash_entries=256 dhash_entries=256 thash_entries=256 "
			"uhash_entries=256 rhash_entries=256 rdinit=/init "
			"initramfs_async=0 tp_enforce tp_hard_p";
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
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	/* Possible CPUs must precede generic per-CPU allocation. */
	tp_smp_discover_cpus();
#endif
	uart_putc_raw('D');

	/*
	 * L-process THDS (optional): leave G null-THDS before this so head
	 * markers stay G-safe; install real THDS so later overflow/illegal
	 * L-trap instead of G-halt (ISM §10.1.3).
	 */
	/* Markers live inside the helper (`t`/`s`/`h`/`L` or `!`+hex).
	 * Do not branch on the C return value: after uart_putc_raw the
	 * AReg residue can make `== 0` look false even when selth stuck. */
	(void)tp_thds_boot_install();
#ifdef CONFIG_TRANSPUTER_SMP_BOOT
	{
		int result = tp_boot_primary();
		unsigned int cpu;
		if (result) panic("Transputer shared CPU bootstrap failed: %d", result);
		for (cpu = 0; cpu < TP_BOOT_CPUS; cpu++)
			pr_info("TP CPU%u: workspace=%08x cookie=%08x iterations=%u parked=%u\n",
				cpu, TP_BOOT->cpu[cpu].workspace, TP_BOOT->cpu[cpu].observed_cookie,
				TP_BOOT->cpu[cpu].iterations, TP_BOOT->cpu[cpu].parked);
		pr_info("TP_SHARED_BOOT: PASS cpus=4 add=4000 cas=4000 lock=4000\n");
	}
#endif

	/*
	 * === Memory initialization (modern NOMMU/memblock pattern) ===
	 *
	 * Transputer memory layout:
	 *   Unified map: RAM_MIB>=24, FB @ 0x81000000, reserve +16..+22 MiB (0x81000000..0x81600000).
	 *   0x80000000  MemStart (MinInt)
	 *   0x80000070  _stext (kernel code start, ELF entry)
	 *   ...         free memory to memory_end
	 *
	 * PAGE_OFFSET = 0, so PFN = addr >> PAGE_SHIFT directly.
	 */

	/* Determine memory bounds - use __linker_end instead of _end
	 * (TCC can't resolve linker script "= .;" symbol assignments) */
	memory_start = PAGE_ALIGN(__linker_end);
	/* Keep this matched to the platform -sm value. A configured size also prevents
	 * early boot from initialising pages that the emulator does not own. */
	memory_end = 0x80000000UL +
		(CONFIG_TRANSPUTER_RAM_MIB * 1024UL * 1024UL);
#ifdef CONFIG_MMU
	/* PGM1 reports the installed banks: RAM0 at 0x80000000 (-sm) and one
	 * bank above 4 GiB (TP_HIGHMEM).  Low memory is RAM0 up to the
	 * window; the rest of RAM0 and the whole high bank are highmem. */
	if (pgm1_read(PGM1_ID) == PGM1_ID_VALUE) {
		u64 ram0_end = 0x80000000ULL + pgm1_read(PGM1_RAM0_SIZE);

		tp_ram0_end = ram0_end;
		tp_high_base = (u64)pgm1_read(PGM1_HIGH_BASE_HI) << 32 |
			       pgm1_read(PGM1_HIGH_BASE_LO);
		tp_high_size = (u64)pgm1_read(PGM1_HIGH_SIZE_HI) << 32 |
			       pgm1_read(PGM1_HIGH_SIZE_LO);
		memory_end = ram0_end > PGM1_LOWMEM_MAX ? PGM1_LOWMEM_MAX :
			     (unsigned long)ram0_end;
	}
#endif
	/* A kernel image (e.g. with an embedded WAD initramfs) larger than
	 * the configured RAM makes end - start wrap negative-unsigned, and
	 * memblock then believes it owns ~2 GiB — the percpu allocator
	 * "stall at mem auto-init, 515304 pages" failure.  Refuse loudly. */
	if (memory_start >= memory_end)
		panic("kernel image ends above CONFIG_TRANSPUTER_RAM_MIB=%d MiB — raise it (and emulator -sm)",
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
	 * FB / compose / scratch — one reserved box (asm/simplefb.h).
	 * emulator order is scratch < FB, so "FB_BASE .. SCRATCH_END" is empty.
	 */
	{
		extern struct memblock memblock;
		struct memblock_region *r = &memblock.reserved.regions[0];
		phys_addr_t rbase = (phys_addr_t)TRANSPUTER_DISP_RESERVE_BASE;
		phys_addr_t rend = (phys_addr_t)TRANSPUTER_DISP_RESERVE_END;
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
		} else
			uart_putc_raw('!'); /* display reserve empty — check simplefb.h */
	}
	uart_putc_raw('2');
	uart_putc_raw('3');
	uart_putc_raw('4');
	min_low_pfn = memory_start >> PAGE_SHIFT;
	max_low_pfn = memory_end >> PAGE_SHIFT;
	max_pfn = max_low_pfn;
#ifdef CONFIG_MMU
	tp_memory_banks();
#endif
	uart_putc_raw('F');

	/*
	 * Set up basic memory zones via paging_init().
	 * On NOMMU this records high_memory.  Linux 7 creates zones in
	 * mm_core_init_early(), immediately after setup_arch() returns.
	 */
	paging_init();
	uart_putc_raw('P');

	/*
	 * Skip calibrate_delay() — software emulator has no timer interrupt
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
