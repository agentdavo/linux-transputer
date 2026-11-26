/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Soft protection contract for Transputer Linux (Phases 2–10).
 *
 * Hardware: TP32X L/P + 4 regions (see transputer/TP32X-PROTECTION.md).
 *
 * Phase 9: stack may live in Q3 LA with phys still in Q2 (region relocate).
 * Phase 10: per-task split notes; soft RX text; soft private-range isolation
 * for concurrent P (uaccess); scheduler can re-install RODS.
 */
#ifndef _ASM_TRANSPUTER_TP_SOFTPROT_H
#define _ASM_TRANSPUTER_TP_SOFTPROT_H

#include <linux/types.h>

/* Soft region — mirrors one TP32X region descriptor after decode.
 * Use plain u8 flags (not bitfields): TCC has historically mishandled
 * bitfield stores, which left RODS as NULL and broke hard P.
 */
struct tp_soft_region {
	u32	phys_base;	/* physical start (reloc field of RODS) */
	u32	la_base;	/* logical start of window (identity iff == phys) */
	u32	size;		/* power of two, >= 256 */
	u32	la_quarter;	/* 0..3 from LA bits 31:30 */
	u8	write;
	u8	exec;
	u8	top;
	u8	device;
	u8	valid;
};

/* Formerly an aligned staging pointer. The low bit distinguishes v1. */
#define TP_TRAP_ABI_ADDR	0x8000006CUL
#define TP_TRAP_ABI_V1		0x54504601u

#define TP_SOFT_NREGIONS	4
#define TP_PDS_WORDS		11

/* ISM 13.2.3: these live registers are outside the hardware PDS.
 * Keep the two native save images together for tp_goprot.S.
 */
struct tp_p_extra {
	u32 fp[7];
	u32 block[3];	/* count, destination stride, source stride */
};

/*
 * Per-task soft protection state (Phase 4 fills this on exec as P-process).
 * Until then, all regions invalid → identity map (current NOMMU behaviour).
 */
struct tp_soft_mm {
	struct tp_soft_region	reg[TP_SOFT_NREGIONS];
	u32			pds[TP_PDS_WORDS];	/* hardware P-process data structure */
	struct tp_p_extra	extra;
	u32			flags;
	/* Phase 9/10: multi-quarter split (per-task — not process-global static). */
	u32			split_phys;
	u32			split_la;
	u32			split_len;
	/* Phase 10: soft RX text [text_lo, text_hi) — reject user writes. */
	u32			text_lo;
	u32			text_hi;
	/*
	 * Phase 10: process-private phys span (image+stack pad).  Soft uaccess
	 * outside private∪shared (FB/VGA/SHM) is rejected so concurrent tasks
	 * cannot poke each other's private RAM via copy_*_user even when HW
	 * Q2 RODS remains a wide window (one region/quarter limit).
	 */
	u32			priv_lo;
	u32			priv_hi;
	/* Optional disjoint workspace allocation for a cached BusyBox applet. */
	u32			stack_base;
	u32			stack_len;
	/* Cached RODS for scheduler re-install without rebuild. */
	u32			rods_cache[4];
};

#define TP_SOFT_FL_ACTIVE	0x1	/* task runs as P-process */
#define TP_SOFT_FL_SYSCALL	0x2	/* prefer trap syscall path */
#define TP_SOFT_FL_SPLIT	0x4	/* data remapped to another quarter */
#define TP_SOFT_FL_PRIV	0x8	/* priv_lo/hi valid for soft isolation */

/* Q3 base for user data/stack when multi-quarter bFLT is active. */
#define TP_USER_DATA_QBASE	0xC0000000u

/* Shared surfaces every P-process may touch (phys / identity LA in Q2). */
#define TP_SHARED_SCRATCH_LO	0x81a00000u
#define TP_SHARED_SCRATCH_HI	0x82000000u	/* scratch+compose+fb carveouts */
#define TP_SHARED_VGA_LO	0xA0000000u
#define TP_SHARED_VGA_HI	0xA0001000u

struct pt_regs;
struct task_struct;

/* Kernel APIs (arch/transputer/kernel/tp_prot.c) */
struct tp_soft_mm *tp_task_soft_mm(struct task_struct *t);
int tp_soft_enforce_enabled(void);
int tp_hard_p_enabled(void);
int tp_soft_setup_user(struct tp_soft_mm *mm, u32 load_addr,
			u32 text_size, u32 data_size, u32 stack_base,
			u32 stack_size);
/* Full 1 GiB identity map of the process load quarter (enforce-friendly). */
int tp_soft_setup_user_quarter(struct tp_soft_mm *mm, u32 load_addr,
				u32 stack_base);
/* Phase 9: record split load before on_exec_finish (from binfmt_flat). */
void tp_note_split_load(u32 data_phys, u32 data_la, u32 data_map_len);
void tp_soft_fill_rods(const struct tp_soft_mm *mm, u32 *rods);
/* Phase 10: install this task's RODS into the emulator (no IPtr change). */
void tp_install_task_rods(struct task_struct *t);
int tp_enter_user_p(struct tp_soft_mm *mm, u32 entry, u32 wsp);
int tp_run_user_until_exit(struct tp_soft_mm *mm, u32 entry, u32 wsp);
/* Phase 7: goprot enter when hard_p, else classic L enter_exec. */
void tp_enter_userspace(unsigned long entry, unsigned long wptr);
void tp_on_exec_finish(struct pt_regs *regs);
void tp_soft_mm_clear(struct task_struct *t);
/* Drop ACTIVE/PRIV so bFLT load put_user is not blocked by prior maps. */
void tp_soft_mm_reset_for_exec(void);

static inline int tp_shared_surface(u32 la, u32 end)
{
	if (la >= TP_SHARED_SCRATCH_LO && end < TP_SHARED_SCRATCH_HI)
		return 1;
	if (la >= TP_SHARED_VGA_LO && end < TP_SHARED_VGA_HI)
		return 1;
	return 0;
}

/*
 * Anonymous memory handed out by mmap2 (uClibc's malloc takes all of its
 * memory that way without an MMU).  The pages are marked, not owned: they
 * are never returned to the kernel, and the hardware Q2 window already
 * lets every process reach them, so the soft check only has to tell them
 * from kernel RAM and from another process's image.
 */
void tp_note_user_mmap(u32 pa, size_t len);
void tp_forget_user_mmap(u32 pa, size_t len);
int tp_user_mmap_ram(u32 pa, size_t len);

/* The image allocation, the optional separate cached-applet workspace,
 * and what the process got from mmap2. */
static inline int tp_private_ram(const struct tp_soft_mm *mm, u32 pa,
				  size_t len)
{
	return (pa >= mm->priv_lo && pa < mm->priv_hi && len <= mm->priv_hi - pa) ||
	       (pa >= mm->stack_base && len <= mm->stack_len &&
		pa - mm->stack_base <= mm->stack_len - len) ||
	       tp_user_mmap_ram(pa, len);
}

/* Access checks used by copy_*_user when ACTIVE. */
static inline int tp_soft_ok(const struct tp_soft_mm *mm, u32 la,
			      size_t len, int write)
{
	unsigned q;
	const struct tp_soft_region *r;
	u32 end, base;
	u32 phys;

	/* Fast path: track-only / no mm → identity (NOMMU compat). */
	if (!mm || !(mm->flags & TP_SOFT_FL_ACTIVE))
		return 1;

	if (len == 0)
		return 1;
	/* Keep NULL / page0 poison even if Q0 holds device maps. */
	if (la < 0x1000u)
		return 0;
	end = la + (u32)len - 1;
	q = (la >> 30) & 3u;
	if (((end >> 30) & 3u) != q)
		return 0; /* crosses quarter */
	r = &mm->reg[q];
	if (!r->valid)
		return 0;
	if (write && !r->write)
		return 0;
	/* LA window: [la_base, la_base+size).  Fall back to phys_base if unset. */
	base = r->la_base ? r->la_base : r->phys_base;
	if (la < base || len > r->size || la - base > r->size - len)
		return 0;

	phys = r->phys_base + (la - base);

	/* Phase 10 soft RX: reject writes into text [text_lo, text_hi). */
	if (write && mm->text_hi > mm->text_lo) {
		if (phys >= mm->text_lo && phys < mm->text_hi)
			return 0;
	}

	/*
	 * Phase 10 soft private isolation (Q2/Q3 user RAM only).
	 * Devices (Q0) and shared FB/VGA always pass once region-valid.
	 */
	if ((mm->flags & TP_SOFT_FL_PRIV) && mm->priv_hi > mm->priv_lo &&
	    q != 0u) {
		if (!tp_shared_surface(phys, phys + (u32)len - 1) &&
		    !tp_shared_surface(la, end))
			return tp_private_ram(mm, phys, len);
	}
	return 1;
}

/*
 * Translate user LA → kernel/phys pointer for remapped regions.
 * Identity when inactive or la_base == phys_base.
 */
static inline u32 tp_la_to_phys(const struct tp_soft_mm *mm, u32 la)
{
	unsigned q;
	const struct tp_soft_region *r;
	u32 la_base;

	if (!mm || !(mm->flags & TP_SOFT_FL_ACTIVE))
		return la;
	q = (la >> 30) & 3u;
	r = &mm->reg[q];
	if (!r->valid)
		return la;
	la_base = r->la_base ? r->la_base : r->phys_base;
	if (r->phys_base == la_base)
		return la; /* identity */
	if (la < la_base || la - la_base >= r->size)
		return la;
	return r->phys_base + (la - la_base);
}

#endif /* _ASM_TRANSPUTER_TP_SOFTPROT_H */
