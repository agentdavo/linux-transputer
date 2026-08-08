/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Soft protection contract for Transputer Linux (Phases 2–9).
 *
 * Hardware: T9000 L/P + 4 regions (see transputer/T9000-PROTECTION.md).
 * Until goprot/syscall are used for every user task, this header documents
 * the software shadow of that model so uaccess and spawn can grow into it.
 *
 * Phase 9: text stays identity in Q2; data/stack may be remapped to Q3 LA
 * with phys_base still in Q2 RAM (T9000 region relocate).  soft_ok checks
 * LA windows; uaccess translates LA→phys when remapped.
 */
#ifndef _ASM_TRANSPUTER_T9000_SOFTPROT_H
#define _ASM_TRANSPUTER_T9000_SOFTPROT_H

#include <linux/types.h>

/* Soft region — mirrors one T9000 region descriptor after decode.
 * Use plain u8 flags (not bitfields): TCC has historically mishandled
 * bitfield stores, which left RODS as NULL and broke hard P.
 */
struct t9k_soft_region {
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

#define T9K_SOFT_NREGIONS	4

/*
 * Per-task soft protection state (Phase 4 fills this on exec as P-process).
 * Until then, all regions invalid → identity map (current NOMMU behaviour).
 */
struct t9k_soft_mm {
	struct t9k_soft_region	reg[T9K_SOFT_NREGIONS];
	u32			pds_phys;	/* guest PDS if using T4 goprot */
	u32			flags;
};

#define T9K_SOFT_FL_ACTIVE	0x1	/* task runs as P-process */
#define T9K_SOFT_FL_SYSCALL	0x2	/* prefer trap syscall path */
#define T9K_SOFT_FL_SPLIT	0x4	/* data remapped to another quarter */

/* Q3 base for user data/stack when multi-quarter bFLT is active. */
#define T9K_USER_DATA_QBASE	0xC0000000u

struct pt_regs;
struct task_struct;

/* Kernel APIs (arch/transputer/kernel/t9000_prot.c) */
struct t9k_soft_mm *t9k_task_soft_mm(struct task_struct *t);
int t9k_soft_enforce_enabled(void);
int t9k_hard_p_enabled(void);
int t9k_soft_setup_user(struct t9k_soft_mm *mm, u32 load_addr,
			u32 text_size, u32 data_size, u32 stack_base,
			u32 stack_size);
/* Full 1 GiB identity map of the process load quarter (enforce-friendly). */
int t9k_soft_setup_user_quarter(struct t9k_soft_mm *mm, u32 load_addr,
				u32 stack_base);
/* Phase 9: record split load before on_exec_finish (from binfmt_flat). */
void t9k_note_split_load(u32 data_phys, u32 data_la, u32 data_map_len);
void t9k_soft_fill_rods(const struct t9k_soft_mm *mm, u32 *rods);
int t9k_enter_user_p(struct t9k_soft_mm *mm, u32 entry, u32 wsp);
int t9k_run_user_until_exit(struct t9k_soft_mm *mm, u32 entry, u32 wsp);
/* Phase 7: goprot enter when hard_p, else classic L enter_exec. */
void t9k_enter_userspace(unsigned long entry, unsigned long wptr);
void t9k_on_exec_finish(struct pt_regs *regs);
void t9k_soft_mm_clear(struct task_struct *t);

/* Access checks used by copy_*_user when ACTIVE. */
static inline int t9k_soft_ok(const struct t9k_soft_mm *mm, u32 la,
			      size_t len, int write)
{
	unsigned q;
	const struct t9k_soft_region *r;
	u32 end, base, lim;

	/* Fast path: track-only / no mm → identity (NOMMU compat). */
	if (!mm || !(mm->flags & T9K_SOFT_FL_ACTIVE))
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
	lim = base + r->size;
	if (la < base || end >= lim)
		return 0;
	return 1;
}

/*
 * Translate user LA → kernel/phys pointer for remapped regions.
 * Identity when inactive or la_base == phys_base.
 */
static inline u32 t9k_la_to_phys(const struct t9k_soft_mm *mm, u32 la)
{
	unsigned q;
	const struct t9k_soft_region *r;
	u32 la_base;

	if (!mm || !(mm->flags & T9K_SOFT_FL_ACTIVE))
		return la;
	q = (la >> 30) & 3u;
	r = &mm->reg[q];
	if (!r->valid)
		return la;
	la_base = r->la_base ? r->la_base : r->phys_base;
	if (r->phys_base == la_base)
		return la; /* identity */
	if (la < la_base || la >= la_base + r->size)
		return la;
	return r->phys_base + (la - la_base);
}

#endif /* _ASM_TRANSPUTER_T9000_SOFTPROT_H */
