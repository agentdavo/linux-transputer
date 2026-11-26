/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PGM1, the TP32X-derived paging extension (emulator PAGING.md).
 *
 * A supervisor-only control page.  P-processes are translated through the
 * user directory (UROOT) when CTRL.USER is set; supervisor addresses at or
 * above KWIN go through the kernel directory (swapper_pg_dir) when
 * CTRL.KWIN is set; everything else the supervisor touches is physical.
 * Physical addresses are 40 bits; RAM above 4 GiB is reached only through
 * a translation.
 */
#ifndef _ASM_TRANSPUTER_PGM1_H
#define _ASM_TRANSPUTER_PGM1_H

#define PGM1_BASE		0x13000000UL
#define PGM1_ID			0x00
#define PGM1_CTRL		0x04
#define PGM1_UROOT		0x08
#define PGM1_KROOT		0x0C
#define PGM1_KWIN		0x10
#define PGM1_FLUSH_ALL		0x14
#define PGM1_FLUSH_VA		0x18
#define PGM1_FAULT_VA		0x1C
#define PGM1_FAULT_ACC		0x20
#define PGM1_FAULT_PC		0x24
#define PGM1_RAM0_BASE		0x28
#define PGM1_RAM0_SIZE		0x2C
#define PGM1_HIGH_BASE_LO	0x30
#define PGM1_HIGH_BASE_HI	0x34
#define PGM1_HIGH_SIZE_LO	0x38
#define PGM1_HIGH_SIZE_HI	0x3C
#define PGM1_PA_BITS		0x40
#define PGM1_STAT_WALKS		0x44
#define PGM1_STAT_FAULTS	0x48
#define PGM1_STAT_CHUNKS	0x4C

#define PGM1_ID_VALUE		0x50474D31U	/* "PGM1" */
#define PGM1_CTRL_USER		0x1U
#define PGM1_CTRL_KWIN		0x2U
#define PGM1_FA_KIND		0x3U		/* 0 fetch, 1 read, 2 write */
#define PGM1_FA_PROT		0x10U
#define PGM1_FA_BUS		0x20U

/* ISM et.* value the platform reports with t.Error for a page fault (extension) */
#define PGM1_ET_PAGEFAULT	0x10
/* StatusReg/ps.Cntl bit: a paged move/move2d is part done (platform-local) */
#define PGM1_CNTL_RESUME	(1U << 29)

/* The supervisor window: vmalloc/ioremap, then the per-CPU frame slots */
#define PGM1_KWIN_BASE		0xF0000000UL
#define PGM1_SLOT_BASE		0xFFC00000UL
#define PGM1_NR_SLOTS		16	/* per CPU: NR_CPUS * 16 pages from the base */
/* Low memory ends below the window, with an 8 MiB guard */
#define PGM1_LOWMEM_MAX		0xEF800000UL

#ifndef __ASSEMBLY__
#include <linux/types.h>

static inline u32 pgm1_read(unsigned int reg)
{
	return *(volatile u32 *)(PGM1_BASE + reg);
}

static inline void pgm1_write(unsigned int reg, u32 val)
{
	*(volatile u32 *)(PGM1_BASE + reg) = val;
}

struct pt_regs;
struct tp_p_extra;

void __init pgm1_paging_init(void);
void pgm1_secondary_init(void);
u64 pgm1_window_pa(unsigned long va);
bool pgm1_present(void);
/* Map one 40-bit frame at a supervisor window slot (non-nesting stack). */
void *pgm1_map_frame(unsigned long pfn);
void pgm1_unmap_frame(void *va);
/* Resolve a user fault in current->mm: 0, or -EFAULT (no VMA, no access,
 * or handle_mm_fault failed). */
int pgm1_handle_user_fault(unsigned long va, unsigned int kind);
/* Run the P-process in pds/extra on current->mm until a trap other than
 * a page fault Linux resolves.  Returns the ISM (et << 8 | reason) pair;
 * *faults counts the page faults resolved on the way. */
int pgm1_run_p(u32 *pds, struct tp_p_extra *extra, unsigned int *faults);
#endif

#endif /* _ASM_TRANSPUTER_PGM1_H */
