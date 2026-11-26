// SPDX-License-Identifier: GPL-2.0
/*
 * Phase 4–6: Linux as TP32X supervisor L-process + soft region MMU.
 *
 * Soft path: per-task thread.soft_mm shadows the 4 TP32X regions; when
 * TP_SOFT_FL_ACTIVE is set, access_ok enforces them (opt-in via
 * kernel cmdline "tp_enforce" or compile TP_SOFT_ENFORCE_DEFAULT).
 *
 * Hard path (emulated goprot/syscall): build PDS/RODS and call
 * tp_asm_goprot(); A/B return the trap reason/type; PDS.cntl saves STATUS.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/signal.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <asm/tp_softprot.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>
#include <asm/unistd.h>
#include <asm/simplefb.h>
#include <asm/timex.h>

extern void do_exit(long code);

/* Match emulator tp32x_protect.h trap causes */
#define TP_TRAP_ACCESS		1
#define TP_TRAP_PRIV		2
#define TP_TRAP_SYSCALL	3
#define TP_TRAP_ERROR		4
#define TP_TRAP_TIMESLICE	5
#define TP_TRAP_BREAKPOINT	6

#define TP_PS_CNTL	0
#define TP_PS_EPTR	1
#define TP_PS_SWPTR	4
#define TP_PS_SIPTR	5
#define TP_PS_SAREG	6
#define TP_PS_SBREG	7
#define TP_PS_SCREG	8
#define TP_NULL_REGION	0x80000000u

/* Provided by tp_goprot.S — real TP32X encodings (60 FA / 60 F8).
 * Returns (et.type << 8) | t.reason composed from the ISM supervisor-resume
 * Areg/Breg on both RTL and emulator. PDS.cntl is architectural StatusReg. */
int tp_asm_goprot(u32 *pds, u32 *rods, struct tp_p_extra *extra);
void tp_asm_init_fp(u32 *fp);
void tp_asm_syscall(void);

/*
 * ISM ABI (RTL and emulator): the P-trap resumes the supervisor with
 * A = t.reason (ISM Table 10.5), B = et.type (Table 10.10); tp_asm_goprot
 * returns them composed.  Map onto the emulator cause codes this file speaks.
 */
static int tp_map_ism_cause(int composed)
{
	unsigned int reason = (unsigned int)composed & 0xffu;
	unsigned int etype  = (unsigned int)composed >> 8;

	switch (reason) {
	case 0x08:			/* t.Scall */
		return TP_TRAP_SYSCALL;
	case 0x06:			/* t.Time */
		return TP_TRAP_TIMESLICE;
	case 0x02:			/* t.Break */
		return TP_TRAP_BREAKPOINT;
	case 0x00:			/* t.Error */
	case 0x0C:			/* t.Cerror */
		if (etype == 0x04)	/* et.AccessViolation */
			return TP_TRAP_ACCESS;
		if (etype == 0x01)	/* et.PrivInstruction */
			return TP_TRAP_PRIV;
		return TP_TRAP_ERROR;
	default:
		return TP_TRAP_ERROR;
	}
}

/* Only precise t.Error FP traps can restart at PDS.Eptr. */
static int tp_fp_signal_code(int composed)
{
	switch (composed) {
	case 0x0700:		/* et.FPError (aggregate) */
	case 0x0800:		/* et.InvalidOperation */
		return FPE_FLTINV;
	case 0x0900:
		return FPE_FLTDIV;
	case 0x0a00:
		return FPE_FLTOVF;
	case 0x0b00:
		return FPE_FLTUND;
	case 0x0c00:
		return FPE_FLTRES;
	default:
		return 0;
	}
}

/*
 * Soft-mm enforcement: 0 = track only (identity access_ok), 1 = ACTIVE.
 * Hard P-process: enter user via goprot (emulated) when ACTIVE.
 * Cmdline: "tp_enforce" and/or "tp_hard_p" (hard implies enforce).
 */
#ifndef TP_SOFT_ENFORCE_DEFAULT
#define TP_SOFT_ENFORCE_DEFAULT	0
#endif
#ifndef TP_HARD_P_DEFAULT
#define TP_HARD_P_DEFAULT	0
#endif
static int tp_soft_enforce = TP_SOFT_ENFORCE_DEFAULT;
static int tp_hard_p = TP_HARD_P_DEFAULT;

/* Registered handlers make the cmdline authoritative and stop the flags
 * being reported as "Unknown kernel command line parameters" and leaking
 * into /init's argv. hard implies enforce. */
static int __init tp_enforce_setup(char *str)
{
	tp_soft_enforce = 1;
	return 1;
}
__setup("tp_enforce", tp_enforce_setup);

static int __init tp_hard_p_setup(char *str)
{
	tp_hard_p = 1;
	tp_soft_enforce = 1;
	return 1;
}
__setup("tp_hard_p", tp_hard_p_setup);

/* Classic L-mode enter (switch.S) — used when hard P is off or goprot no-ops. */
extern void __transputer_enter_exec(unsigned long entry, unsigned long wptr);

struct tp_soft_mm *tp_task_soft_mm(struct task_struct *t)
{
	if (!t)
		t = current;
	return &t->thread.soft_mm;
}

/* One bit a page of RAM: set for pages mmap2 gave to user space. */
#define TP_RAM_BASE	0x80000000u
#define TP_RAM_PAGES	((CONFIG_TRANSPUTER_RAM_MIB << 20) >> PAGE_SHIFT)
static DECLARE_BITMAP(tp_mmap_pages, TP_RAM_PAGES);
/* Mapping release may run on another CPU even while userspace is pinned.
 * Serialize whole ranges so a reader never observes a partial update.
 * This lock protects metadata only; allocation ownership remains with mm.
 */
static DEFINE_RAW_SPINLOCK(tp_mmap_pages_lock);

static void tp_set_user_mmap(u32 pa, size_t len, bool mapped)
{
	u32 first = (pa - TP_RAM_BASE) >> PAGE_SHIFT;
	u32 n = PAGE_ALIGN(len) >> PAGE_SHIFT;
	unsigned long flags;

	if (pa < TP_RAM_BASE || first >= TP_RAM_PAGES ||
	    n > TP_RAM_PAGES - first)
		return;
	raw_spin_lock_irqsave(&tp_mmap_pages_lock, flags);
	if (mapped)
		bitmap_set(tp_mmap_pages, first, n);
	else
		bitmap_clear(tp_mmap_pages, first, n);
	raw_spin_unlock_irqrestore(&tp_mmap_pages_lock, flags);
}

void tp_note_user_mmap(u32 pa, size_t len)
{
	tp_set_user_mmap(pa, len, true);
}

void tp_forget_user_mmap(u32 pa, size_t len)
{
	tp_set_user_mmap(pa, len, false);
}

int tp_user_mmap_ram(u32 pa, size_t len)
{
	u32 first, last;
	unsigned long flags;
	int mapped;

	if (!len || pa < TP_RAM_BASE || pa + (u32)len - 1 < pa)
		return 0;
	first = (pa - TP_RAM_BASE) >> PAGE_SHIFT;
	last = (pa + (u32)len - 1 - TP_RAM_BASE) >> PAGE_SHIFT;
	if (last >= TP_RAM_PAGES)
		return 0;
	raw_spin_lock_irqsave(&tp_mmap_pages_lock, flags);
	mapped = find_next_zero_bit(tp_mmap_pages, last + 1, first) > last;
	raw_spin_unlock_irqrestore(&tp_mmap_pages_lock, flags);
	return mapped;
}
EXPORT_SYMBOL(tp_user_mmap_ram);

int tp_soft_enforce_enabled(void)
{
	return tp_soft_enforce;
}
EXPORT_SYMBOL(tp_soft_enforce_enabled);

int tp_hard_p_enabled(void)
{
	return tp_hard_p;
}
EXPORT_SYMBOL(tp_hard_p_enabled);

/* Round size up to power-of-two >= 256 */
static u32 pow2_ge(u32 s)
{
	u32 n = 256;
	if (s < 256)
		return 256;
	while (n < s && n < (1u << 30))
		n <<= 1;
	return n;
}

/*
 * Build ISM §9.5 region descriptor for identity map of [phys, phys+size).
 * size is rounded up to 2^n (n>=8).  reloc holds high phys bits.
 */
static u32 make_desc(u32 phys, u32 size, int write, int exec)
{
	u32 n = 8;
	u32 sz = pow2_ge(size);
	u32 d, size_bit, reloc;

	while ((1u << n) < sz && n < 30)
		n++;
	size_bit = 1u << (n - 1);
	phys &= ~((1u << n) - 1u);
	reloc = phys;
	d = reloc | size_bit;
	if (write)
		d |= 1u;
	if (exec)
		d |= 2u;
	return d;
}

/*
 * Identity-map an entire address quarter (1 GiB) RWX.
 * Safe first-step enforce for TputeOS: all bFLT, FB, SHM, and VGA
 * control live in quarter 2 (0x80000000–0xBFFFFFFF).  Quarters 0/1/3
 * stay unmapped so stray low/high user pointers fail access_ok.
 */
static void soft_map_quarter(struct tp_soft_mm *mm, unsigned q,
			     int write, int exec)
{
	u32 base;

	if (q > 3)
		return;
	base = (u32)q << 30;
	mm->reg[q].phys_base = base;
	mm->reg[q].la_base = base;
	mm->reg[q].size = 1u << 30;
	mm->reg[q].la_quarter = q;
	mm->reg[q].write = write ? 1 : 0;
	mm->reg[q].exec = exec ? 1 : 0;
	mm->reg[q].valid = 1;
}

/*
 * Tight map: image [load, load+text+data) plus stack window.
 * Used for tracking; enforce prefers quarter identity (below).
 */
int tp_soft_setup_user(struct tp_soft_mm *mm, u32 load_addr,
			u32 text_size, u32 data_size, u32 stack_base,
			u32 stack_size)
{
	u32 code_sz, st_sz;
	unsigned q_code, q_stack;

	if (!mm)
		return -EINVAL;
	memset(mm, 0, sizeof(*mm));

	code_sz = pow2_ge(text_size + data_size);
	st_sz = pow2_ge(stack_size);

	q_code = (load_addr >> 30) & 3u;
	q_stack = (stack_base >> 30) & 3u;

	mm->reg[q_code].phys_base = load_addr & ~(code_sz - 1);
	mm->reg[q_code].la_base = mm->reg[q_code].phys_base;
	mm->reg[q_code].size = code_sz;
	mm->reg[q_code].la_quarter = q_code;
	mm->reg[q_code].write = 1;
	mm->reg[q_code].exec = 1;
	mm->reg[q_code].valid = 1;

	if (q_stack != q_code) {
		mm->reg[q_stack].phys_base = stack_base & ~(st_sz - 1);
		mm->reg[q_stack].la_base = mm->reg[q_stack].phys_base;
		mm->reg[q_stack].size = st_sz;
		mm->reg[q_stack].la_quarter = q_stack;
		mm->reg[q_stack].write = 1;
		mm->reg[q_stack].exec = 0;
		mm->reg[q_stack].valid = 1;
	} else {
		u32 end = stack_base + st_sz;
		u32 base = mm->reg[q_code].phys_base;
		if (end > base + mm->reg[q_code].size)
			mm->reg[q_code].size = pow2_ge(end - base);
	}

	mm->flags = TP_SOFT_FL_ACTIVE | TP_SOFT_FL_SYSCALL;
	return 0;
}

/*
 * Map a power-of-two window inside quarter @q covering [cover_lo, cover_hi).
 * TP32X: one region per LA quarter — base is aligned down to @sz within the
 * quarter (bottom-style identity for that window).
 */
static void soft_map_quarter_window(struct tp_soft_mm *mm, unsigned q,
				    u32 cover_lo, u32 cover_hi,
				    int write, int exec)
{
	u32 qbase = (u32)q << 30;
	u32 qlim = qbase + (1u << 30);
	u32 lo, hi, need, sz, base;

	if (q > 3)
		return;
	lo = cover_lo;
	hi = cover_hi;
	if (lo < qbase)
		lo = qbase;
	if (hi > qlim)
		hi = qlim;
	if (hi <= lo)
		hi = lo + 0x100000u;
	need = hi - lo;
	sz = pow2_ge(need);
	if (sz > (1u << 30))
		sz = 1u << 30;
	/* Align base down within quarter so [base, base+sz) covers [lo, hi). */
	base = lo & ~(sz - 1u);
	if (base < qbase)
		base = qbase;
	if (base + sz < hi) {
		/* Need larger power-of-two */
		while (base + sz < hi && sz < (1u << 30))
			sz <<= 1;
		base = lo & ~(sz - 1u);
		if (base < qbase)
			base = qbase;
	}
	mm->reg[q].phys_base = base;
	mm->reg[q].la_base = base; /* identity window */
	mm->reg[q].size = sz;
	mm->reg[q].la_quarter = q;
	mm->reg[q].write = write ? 1 : 0;
	mm->reg[q].exec = exec ? 1 : 0;
	mm->reg[q].valid = 1;
}

/*
 * Remapped window: LA [la_base, la_base+sz) → phys [phys_base, +sz).
 * Used for Q3 data with Q2 physical pages (TP32X region relocate).
 */
static void soft_map_remap(struct tp_soft_mm *mm, unsigned q,
			   u32 la_base, u32 phys_base, u32 size,
			   int write, int exec)
{
	u32 sz;

	if (q > 3 || !size)
		return;
	sz = pow2_ge(size);
	if (sz > (1u << 30))
		sz = 1u << 30;
	phys_base &= ~(sz - 1u);
	la_base &= ~(sz - 1u);
	mm->reg[q].phys_base = phys_base;
	mm->reg[q].la_base = la_base;
	mm->reg[q].size = sz;
	mm->reg[q].la_quarter = q;
	mm->reg[q].write = write ? 1 : 0;
	mm->reg[q].exec = exec ? 1 : 0;
	mm->reg[q].valid = 1;
}

/*
 * Enforce-friendly map for process load (+ stack) quarter(s).
 *
 * Q2: text/data/FB/SHM/VGA (typical bFLT load quarter).
 * Q0: device MMIO (console UART 0x10M, kbd 0x11M, mouse 0x12M, SD 0x20M)
 *     mapped as full-quarter RW no-X so hard-P goprot allows console I/O.
 *     soft_ok still rejects la < 0x1000 (NULL poison).
 * When text/data sit in different quarters, RX vs RW is applied later.
 */
int tp_soft_setup_user_quarter(struct tp_soft_mm *mm, u32 load_addr,
				u32 stack_base)
{
	unsigned q_code, q_stack;
	u32 lo, hi;

	if (!mm)
		return -EINVAL;
	memset(mm, 0, sizeof(*mm));

	q_code = (load_addr >> 30) & 3u;
	q_stack = (stack_base >> 30) & 3u;

	/* Process-local span */
	lo = load_addr & ~0xfffffu; /* 1 MiB floor */
	hi = load_addr + 0x200000u; /* 2 MiB image headroom */
	if (stack_base < lo)
		lo = stack_base & ~0xfffffu;
	if (stack_base + 0x20000u > hi)
		hi = stack_base + 0x20000u;

	/* Shared TputeOS surfaces (same quarter as typical bFLT). */
	if ((u32)TRANSPUTER_SCRATCH_BASE < lo)
		lo = (u32)TRANSPUTER_SCRATCH_BASE;
	if ((u32)TRANSPUTER_FB_BASE + (u32)TRANSPUTER_FB_CARVEOUT_SIZE > hi)
		hi = (u32)TRANSPUTER_FB_BASE + (u32)TRANSPUTER_FB_CARVEOUT_SIZE;
	if (0xA0000020u > hi)
		hi = 0xA0000020u;

	soft_map_quarter_window(mm, q_code, lo, hi, 1, 1);
	if (q_stack != q_code) {
		u32 slo = stack_base & ~0xffffu;
		u32 shi = stack_base + 0x20000u;
		soft_map_quarter_window(mm, q_stack, slo, shi, 1, 0);
	}

	/* Device MMIO in Q0 — required under hard P (init console UART). */
	if (q_code != 0 && q_stack != 0)
		soft_map_quarter(mm, 0, 1, 0);

	mm->flags = TP_SOFT_FL_ACTIVE | TP_SOFT_FL_SYSCALL;
	return 0;
}
EXPORT_SYMBOL(tp_soft_setup_user_quarter);

/*
 * binfmt_flat records a multi-quarter load (data phys in Q2, LA in Q3).
 * Notes live in per-task soft_mm (Phase 10 — no process-global static race).
 * Do NOT set ACTIVE here — reloc/GOT still touch Q2 phys via put_user.
 */
void tp_note_split_load(u32 data_phys, u32 data_la, u32 data_map_len)
{
	struct tp_soft_mm *mm = tp_task_soft_mm(current);

	if (!mm || !data_phys || !data_la || !data_map_len)
		return;
	mm->split_phys = data_phys;
	mm->split_la = data_la;
	mm->split_len = data_map_len;
	mm->flags |= TP_SOFT_FL_SPLIT;
}
EXPORT_SYMBOL(tp_note_split_load);

/* Clear soft-mm (spawn/task teardown). */
void tp_soft_mm_clear(struct task_struct *t)
{
	struct tp_soft_mm *mm = tp_task_soft_mm(t);

	if (mm)
		memset(mm, 0, sizeof(*mm));
}
EXPORT_SYMBOL(tp_soft_mm_clear);

/*
 * Phase 10: exec load path does put_user into the new image.  Old PRIV maps
 * would reject those addresses (EFAULT on binfmt read).  Clear without
 * leave_p — caller may still be in a parent P session.
 */
void tp_soft_mm_reset_for_exec(void)
{
	struct tp_soft_mm *mm = tp_task_soft_mm(current);

	if (mm)
		memset(mm, 0, sizeof(*mm));
}
EXPORT_SYMBOL(tp_soft_mm_reset_for_exec);

/*
 * setup_new_exec() runs this after the point of no return and before the
 * loader writes anything into the new image.
 */
void arch_setup_new_exec(void)
{
	current->thread.user_tp = 0;
#ifndef CONFIG_MMU
	tp_soft_mm_reset_for_exec();
#endif
}

void tp_soft_fill_rods(const struct tp_soft_mm *mm, u32 *rods)
{
	int i;
	if (!rods)
		return;
	for (i = 0; i < 4; i++) {
		u32 sz, d;

		if (!mm || !mm->reg[i].valid) {
			rods[i] = TP_NULL_REGION;
			continue;
		}
		sz = mm->reg[i].size;
		/* Reject nonsense sizes (TCC/layout glitches have produced ~0). */
		if (sz < 256u || sz > (1u << 30)) {
			rods[i] = TP_NULL_REGION;
			continue;
		}
		d = make_desc(mm->reg[i].phys_base, sz,
			      mm->reg[i].write, mm->reg[i].exec);
		if (d == 0u || d == 0xffffffffu)
			d = TP_NULL_REGION;
		rods[i] = d;
	}
	/*
	 * Q2: always pin full-quarter RWX (text+FB+VGA).  make_desc(1<<30)
	 * has been flaky under TCC (seen as 0xffffffff in UART logs);
	 * 0xA0000003 is the known-good ISM encoding for Q2 1GiB W|X.
	 */
	rods[2] = 0xa0000003u;
}
EXPORT_SYMBOL(tp_soft_fill_rods);

/*
 * Phase 10: push this task's cached RODS + PMI private/text into the emulator
 * without changing IPtr/WPtr.  Used on context switch.
 */
#define TP_LOAD_RODS_MMIO	0xA0000020u
#define TP_LOAD_RODS_SLOT	0xA0000024u	/* +0..12 = four RODS words */
#define TP_PMI_PRIV_LO		0xA0000034u
#define TP_PMI_PRIV_HI		0xA0000038u
#define TP_PMI_TEXT_LO		0xA000003Cu
#define TP_PMI_TEXT_HI		0xA0000040u
#define TP_PMI_COMMIT		0xA0000044u

void tp_install_task_rods(struct task_struct *t)
{
	struct tp_soft_mm *mm = tp_task_soft_mm(t);
	int i;

	if (!mm || !(mm->flags & TP_SOFT_FL_ACTIVE))
		return;
	if (!mm->rods_cache[0] && !mm->rods_cache[2] && !mm->rods_cache[3])
		tp_soft_fill_rods(mm, mm->rods_cache);

	for (i = 0; i < 4; i++)
		*(volatile u32 *)(TP_LOAD_RODS_SLOT + (u32)(4 * i)) =
			mm->rods_cache[i];
	*(volatile u32 *)TP_LOAD_RODS_MMIO = 1;

	/* PMI per-process private + text-as-ROM under P */
	if (mm->flags & TP_SOFT_FL_PRIV) {
		*(volatile u32 *)TP_PMI_PRIV_LO = mm->priv_lo;
		*(volatile u32 *)TP_PMI_PRIV_HI = mm->priv_hi;
	} else {
		*(volatile u32 *)TP_PMI_PRIV_LO = 0;
		*(volatile u32 *)TP_PMI_PRIV_HI = 0;
	}
	*(volatile u32 *)TP_PMI_TEXT_LO = mm->text_lo;
	*(volatile u32 *)TP_PMI_TEXT_HI = mm->text_hi;
	*(volatile u32 *)TP_PMI_COMMIT = 1;
}
EXPORT_SYMBOL(tp_install_task_rods);

/* Called only at the scheduler boundary, on the physical kernel workspace.
 * L-mode still relocates addresses after a legacy leave_p, so an inactive
 * task must not inherit the previous task's Q3 alias.  Do not clear these
 * registers in leave_p itself: that path may still run on a Q3 workspace.
 */
void tp_switch_task_rods(struct task_struct *t)
{
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	struct tp_soft_mm *mm = tp_task_soft_mm(t);
	int i;

	if (!mm || !(mm->flags & TP_SOFT_FL_ACTIVE)) {
		for (i = 0; i < 4; i++)
			*(volatile u32 *)(TP_LOAD_RODS_SLOT + (u32)(4 * i)) =
				0x80000000u; /* ISM null region descriptor */
		*(volatile u32 *)TP_LOAD_RODS_MMIO = 1;
		*(volatile u32 *)TP_PMI_PRIV_LO = 0;
		*(volatile u32 *)TP_PMI_PRIV_HI = 0;
		*(volatile u32 *)TP_PMI_TEXT_LO = 0;
		*(volatile u32 *)TP_PMI_TEXT_HI = 0;
		*(volatile u32 *)TP_PMI_COMMIT = 1;
		return;
	}
#endif
	tp_install_task_rods(t);
}

static void init_p_state(struct tp_soft_mm *mm, u32 entry, u32 wsp)
{
	u32 *pds = mm->pds;
	int i;
	memset(&mm->extra, 0, sizeof(mm->extra));
	tp_asm_init_fp(mm->extra.fp);
	for (i = 0; i < TP_PDS_WORDS; i++)
		pds[i] = 0;
	/* Explicit indices — match emulator tp32x_protect.h */
	pds[4] = wsp;	/* SWPTR */
	pds[5] = entry;	/* SIPTR */
	/* ps.Cntl is the control word goprot loads into StatusReg (ISM Table
	 * 9.3): default CW = IntOvTeBit only, matching a fresh L-process. */
	pds[0] = 0x00000080u;
}

/*
 * Soft enter: mark ACTIVE and leave regs for classic return-to-user.
 * Hard enter: goprot once; returns trap cause (3 = syscall).
 */
int tp_enter_user_p(struct tp_soft_mm *mm, u32 entry, u32 wsp)
{
	u32 *pds, *rods;
	u32 cause;

	if (!mm || !(mm->flags & TP_SOFT_FL_ACTIVE))
		return -EINVAL;

	pds = mm->pds;
	rods = mm->rods_cache;
	tp_soft_fill_rods(mm, rods);
	init_p_state(mm, entry, wsp);

	/* Both emulation and hardware return the ISM reason/type pair. */
	cause = (u32)tp_map_ism_cause(
		tp_asm_goprot(pds, rods, &mm->extra));
	if (cause == 0) {
		pr_info_once("tp32x_prot: soft P active entry=%08x wsp=%08x\n",
			     entry, wsp);
		return 0;
	}
	return (int)cause;
}

extern long __transputer_syscall(long, long, long, long, long, long, long);

/* A normal C handler enters with W[0..3] holding return address and args.
 * Its ret advances Wptr past those four words, to the sigreturn context.
 */
/* Matches the static uClibc Transputer ucontext_t layout. */
struct tp_signal_context {
	unsigned long flags;
	unsigned long link;
	stack_t stack;
	struct sigcontext regs;
	sigset_t mask;
};

struct tp_signal_frame {
	u32 linkage[4];
	struct tp_signal_context context;
	struct tp_p_extra extra;
	siginfo_t info;
};

static bool tp_signal_text(const struct tp_soft_mm *mm, u32 addr)
{
	u32 pa = tp_la_to_phys(mm, addr);

	return mm->reg[addr >> 30].exec && tp_soft_ok(mm, addr, 1, 0) &&
		pa >= mm->text_lo && pa < mm->text_hi;
}

static bool tp_signal_ram(const struct tp_soft_mm *mm, u32 addr, size_t size)
{
	u32 pa = tp_la_to_phys(mm, addr);

	return tp_soft_ok(mm, addr, size, 1) &&
		tp_private_ram(mm, pa, size) &&
		(pa >= mm->text_hi || (pa < mm->text_lo && size <= mm->text_lo - pa));
}

static long tp_sigaction(struct tp_soft_mm *mm, const long *args)
{
	const struct sigaction __user *act = (const void __user *)args[1];
	struct sigaction __user *old = (void __user *)args[2];
	struct k_sigaction new_sa, old_sa;
	int ret;

	if (args[3] != sizeof(sigset_t))
		return -EINVAL;
	if (old && !tp_signal_ram(mm, (u32)old, sizeof(*old)))
		return -EFAULT;
	if (act) {
		if (copy_from_user(&new_sa.sa, act, sizeof(*act)))
			return -EFAULT;
		if (new_sa.sa.sa_handler != SIG_DFL && new_sa.sa.sa_handler != SIG_IGN) {
			if (!(new_sa.sa.sa_flags & SA_RESTORER)) {
				new_sa.sa.sa_flags |= SA_RESTORER;
				new_sa.sa.sa_restorer = (__sigrestore_t)args[4];
			}
			if (!tp_signal_text(mm, (u32)new_sa.sa.sa_handler) ||
			    !tp_signal_text(mm, (u32)new_sa.sa.sa_restorer))
				return -EINVAL;
		}
	}
	ret = do_sigaction((int)args[0], act ? &new_sa : NULL, old ? &old_sa : NULL);
	if (!ret && old && copy_to_user(old, &old_sa.sa, sizeof(*old)))
		return -EFAULT;
	return ret;
}

static int tp_setup_signal(struct tp_soft_mm *mm, struct ksignal *ksig)
{
	struct tp_signal_frame __user *user_frame;
	struct tp_signal_context context = {};
	u32 *pds = mm->pds;
	u32 linkage[4];
	u32 wsp = pds[TP_PS_SWPTR];
	u32 addr;

	if (wsp < sizeof(*user_frame))
		return -EFAULT;
	addr = (wsp - sizeof(*user_frame)) & ~7u;
	user_frame = (void __user *)addr;
	if (!tp_signal_ram(mm, addr, sizeof(*user_frame)) ||
	    !tp_signal_text(mm, (u32)ksig->ka.sa.sa_handler) ||
	    !tp_signal_text(mm, (u32)ksig->ka.sa.sa_restorer))
		return -EFAULT;

	context.stack.ss_flags = SS_DISABLE;
	context.regs.wptr = wsp;
	context.regs.iptr = pds[TP_PS_SIPTR];
	context.regs.areg = pds[TP_PS_SAREG];
	context.regs.breg = pds[TP_PS_SBREG];
	context.regs.creg = pds[TP_PS_SCREG];
	context.regs.status = pds[TP_PS_CNTL];
	context.mask = *sigmask_to_save();
	linkage[0] = (u32)ksig->ka.sa.sa_restorer;
	linkage[1] = ksig->sig;
	linkage[2] = (u32)&user_frame->info;
	linkage[3] = (u32)&user_frame->context;
	if (copy_to_user(user_frame->linkage, linkage, sizeof(linkage)) ||
	    copy_to_user(&user_frame->context, &context, sizeof(context)) ||
	    copy_to_user(&user_frame->extra, &mm->extra, sizeof(mm->extra)) ||
	    ((ksig->ka.sa.sa_flags & SA_SIGINFO) &&
	     copy_siginfo_to_user(&user_frame->info, &ksig->info)))
		return -EFAULT;

	pds[TP_PS_SWPTR] = addr;
	pds[TP_PS_SIPTR] = (u32)ksig->ka.sa.sa_handler;
	pds[TP_PS_SAREG] = linkage[0];
	pds[TP_PS_SBREG] = 0;
	pds[TP_PS_SCREG] = 0;
	/* The saved context retains the interrupted enables for sigreturn. */
	pds[TP_PS_CNTL] &= ~0x0002aa08u;
	tp_asm_init_fp(mm->extra.fp);
	memset(mm->extra.block, 0, sizeof(mm->extra.block));
	return 0;
}

static int tp_sigreturn(struct tp_soft_mm *mm)
{
	struct tp_signal_frame __user *user_frame;
	struct tp_signal_context context;
	struct tp_p_extra extra;
	u32 *pds = mm->pds;
	u32 wsp = pds[TP_PS_SWPTR];
	u32 addr = wsp - sizeof(user_frame->linkage);

	user_frame = (void __user *)addr;
	if ((addr & 7) || addr > wsp ||
	    !tp_signal_ram(mm, addr, sizeof(*user_frame)) ||
	    copy_from_user(&context, &user_frame->context, sizeof(context)) ||
	    copy_from_user(&extra, &user_frame->extra, sizeof(extra)) ||
	    (context.regs.wptr & 3) ||
	    !tp_signal_ram(mm, context.regs.wptr, sizeof(u32)) ||
	    !tp_signal_text(mm, context.regs.iptr) ||
	    (extra.fp[0] & ~0x57u)) {
		force_sig(SIGSEGV);
		return -EFAULT;
	}

	current->restart_block.fn = do_no_restart_syscall;
	set_current_blocked(&context.mask);
	pds[TP_PS_SWPTR] = context.regs.wptr;
	pds[TP_PS_SIPTR] = context.regs.iptr;
	pds[TP_PS_SAREG] = context.regs.areg;
	pds[TP_PS_SBREG] = context.regs.breg;
	pds[TP_PS_SCREG] = context.regs.creg;
	/* stflags permits these error flags and trap enables in P-mode.
	 * Never import privilege, timeslice or debug controls from userspace. */
	pds[TP_PS_CNTL] = (pds[TP_PS_CNTL] & ~0x000bffccu) |
		(context.regs.status & 0x000bffccu);
	mm->extra = extra;
	return 0;
}

static void tp_resume_user(struct tp_soft_mm *mm, int cause, long nr, long ret)
{
	struct ksignal ksig;
	bool deliver = signal_pending(current) && get_signal(&ksig);
	u32 *pds = mm->pds;

	if (cause == TP_TRAP_SYSCALL) {
		switch (nr == __NR_rt_sigreturn ? 0 : ret) {
		case -ERESTART_RESTARTBLOCK:
		case -ERESTARTNOHAND:
		case -ERESTARTSYS:
		case -ERESTARTNOINTR:
			if (deliver && ret != -ERESTARTNOINTR &&
			    (ret != -ERESTARTSYS || !(ksig.ka.sa.sa_flags & SA_RESTART))) {
				ret = -EINTR;
				break;
			}
			pds[TP_PS_SIPTR] = pds[TP_PS_EPTR];
			ret = ret == -ERESTART_RESTARTBLOCK ? __NR_restart_syscall : nr;
			break;
		}
		pds[TP_PS_SAREG] = ret;
	}
	while (deliver) {
		int err = tp_setup_signal(mm, &ksig);

		signal_setup_done(err, &ksig, 0);
		if (!err)
			return;
		/* A bad frame queued SIGSEGV. Handle it before resuming P. */
		deliver = get_signal(&ksig);
	}
	restore_saved_sigmask();
}

/* Read all six arguments before dispatch can change the current mapping.
 * A trap frame must be in this process's RAM, even on configurations
 * where ordinary user access also permits framebuffer or device windows.
 */
static long tp_dispatch_frame(struct tp_soft_mm *mm, long nr, u32 la)
{
	long args[6];
	u32 pa;

	if ((la & 3) || !(mm->flags & TP_SOFT_FL_PRIV) ||
	    !tp_soft_ok(mm, la, sizeof(args), 0))
		return -EFAULT;
	pa = tp_la_to_phys(mm, la);
	if (!tp_private_ram(mm, pa, sizeof(args)))
		return -EFAULT;

	memcpy(args, (const void *)(unsigned long)pa, sizeof(args));
	if (nr == __NR_rt_sigaction)
		return tp_sigaction(mm, args);
	if (nr == __NR_restart_syscall)
		return sys_restart_syscall();
	if (nr == __NR_clone) {
		struct pt_regs *regs = current_pt_regs();
		u32 stack = args[1];
		struct kernel_clone_args child = {
			.flags = args[0] & ~CSIGNAL,
			.exit_signal = args[0] & CSIGNAL,
			.stack = stack,
			.pidfd = (int __user *)args[2],
			.parent_tid = (int __user *)args[2],
			.child_tid = (int __user *)args[3],
			.tls = args[4],
		};

		if (!(args[0] & CLONE_THREAD))
			return -EINVAL;
		if ((stack & 3) || stack < 4 || !tp_soft_ok(mm, stack - 4, 20, 1) ||
		    !tp_private_ram(mm, tp_la_to_phys(mm, stack - 4), 20))
			return -EFAULT;
		/* The trap, rather than the original exec frame, is the clone site. */
		regs->iptr = mm->pds[TP_PS_SIPTR];
		regs->wptr = mm->pds[TP_PS_SWPTR];
		regs->status = 1;
		return kernel_clone(&child);
	}
	return __transputer_syscall(nr, args[0], args[1], args[2],
				    args[3], args[4], args[5]);
}

/*
 * Run a user P-session: goprot → user until trap.
 *
 * On the emulator, P-trap longjmps to the emulator mainloop with IPtr restored to the
 * instruction after goprot. tp_asm_goprot() returns the ISM reason/type
 * pair, while PDS.cntl preserves the protected StatusReg.
 *
 * cause 0  → goprot was a no-op (TP_PROTECT=0): caller should classic-enter.
 * cause 3  → syscall trap: dispatch, write return to sareg, re-goprot.
 * cause 5  → timeslice trap: service timers, schedule and resume P.
 * other    → access/priv/etc: return cause to caller.
 *
 * Do NOT refill PDS on restart — trap already updated siptr/sareg/swptr.
 */
int tp_run_user_until_exit(struct tp_soft_mm *mm, u32 entry, u32 wsp)
{
	u32 *pds, *rods;
	int cause;
	int restarts = 0;

	if (!mm || !(mm->flags & TP_SOFT_FL_ACTIVE))
		return -EINVAL;

	pds = mm->pds;
	rods = mm->rods_cache;
	tp_soft_fill_rods(mm, rods);
	init_p_state(mm, entry, wsp);

	for (;;) {
		int trap = tp_asm_goprot(pds, rods, &mm->extra);
		int fp_code = tp_fp_signal_code(trap);

		cause = tp_map_ism_cause(trap);
		if (fp_code) {
			pds[TP_PS_SIPTR] = pds[TP_PS_EPTR];
			force_sig_fault(SIGFPE, fp_code,
					(void __user *)pds[TP_PS_EPTR]);
			transputer_poll_tick();
			tp_resume_user(mm, cause, 0, 0);
			continue;
		}

		if (cause == 0)
			return 0; /* soft no-op — fall back to L enter */
		if (cause != TP_TRAP_SYSCALL && cause != TP_TRAP_TIMESLICE)
			return cause;

		transputer_poll_tick();
		if (cause == TP_TRAP_TIMESLICE) {
			cond_resched();
			tp_resume_user(mm, cause, 0, 0);
		} else {
			long nr = pds[TP_PS_SAREG];
			long ret;

			if (nr == __NR_rt_sigreturn) {
				ret = tp_sigreturn(mm);
				if (!ret)
					ret = (long)pds[TP_PS_SAREG];
			} else {
				ret = tp_dispatch_frame(mm, nr, pds[TP_PS_SBREG]);
			}
			tp_resume_user(mm, cause, nr, ret);
			if (++restarts > 100000)
				return -EAGAIN;
		}
	}
}

/*
 * Unified userspace entry (Phase 7).
 *
 * Prefer hard P-process via goprot when tp_hard_p && ACTIVE.  If goprot
 * is a no-op (cause 0), fall through to classic L-mode enter_exec.
 * Never returns on a successful classic enter.
 */
void tp_enter_userspace(unsigned long entry, unsigned long wptr)
{
	struct tp_soft_mm *mm = tp_task_soft_mm(current);
	int cause;

	if (tp_hard_p && mm && (mm->flags & TP_SOFT_FL_ACTIVE)) {
		static atomic_t p_enters = ATOMIC_INIT(0);
		unsigned long enters;

		enters = atomic_inc_return(&p_enters);
		if (enters == 1 || (enters & 0x3f) == 0)
			pr_info("tp32x_prot: P-enter #%lu entry=%08lx wsp=%08lx\n",
				enters, entry, wptr);

		/* Bind RODS + PMI private/text before first goprot. */
		tp_install_task_rods(current);

		cause = tp_run_user_until_exit(mm, (u32)entry, (u32)wptr);
		if (cause == 0) {
			/* TP_PROTECT off or goprot soft — classic enter */
			pr_info_once("tp32x_prot: hard-P goprot no-op → classic enter\n");
			__transputer_enter_exec(entry, wptr);
			return;
		}
		/*
		 * Session ended by non-syscall trap.  If spawn longjmp already
		 * left this stack, we never get here.  Otherwise exit the task.
		 */
		pr_info("tp32x_prot: P-session end cause=%d entry=%08lx eptr=%08x siptr=%08x swptr=%08x\n",
			cause, entry, mm->pds[TP_PS_EPTR],
			mm->pds[TP_PS_SIPTR], mm->pds[TP_PS_SWPTR]);
		do_exit(cause);
	}

	__transputer_enter_exec(entry, wptr);
}
EXPORT_SYMBOL(tp_enter_userspace);

/* Called from start_thread / transputer_flat_finish after regs are primed */
void tp_on_exec_finish(struct pt_regs *regs)
{
	struct tp_soft_mm *mm = tp_task_soft_mm(current);
	struct mm_struct *m = current->mm;
	u32 load, stack_base, stack_top, stack_phys;
	u32 split_phys, split_la, split_len;
	u32 saved_flags_split;
	u32 priv_lo, priv_hi, text_lo, text_hi;

	if (!m || !regs || !mm)
		return;

	/* Preserve split notes across setup_user_quarter memset. */
	saved_flags_split = mm->flags & TP_SOFT_FL_SPLIT;
	split_phys = mm->split_phys;
	split_la = mm->split_la;
	split_len = mm->split_len;

	load = (u32)m->start_code;
	if (!load)
		load = (u32)regs->iptr;

	stack_top = (u32)regs->wptr;
	/* Stack grows down; base for quarter select is near top. */
	stack_base = (stack_top > 0x1000) ? (stack_top - 0x1000) : stack_top;

	/*
	 * Range calc must use phys stack so Q3 LA does not pull Q2 lo/hi
	 * into the upper half of the address space.
	 */
	stack_phys = stack_base;
	if (((stack_base >> 30) & 3u) == 3u && split_phys && split_la &&
	    split_len && stack_base >= split_la &&
	    stack_base < split_la + split_len)
		stack_phys = split_phys + (stack_base - split_la);
	else if (((stack_base >> 30) & 3u) == 3u)
		stack_phys = stack_base - 0x40000000u; /* full Q2 mirror */

	/*
	 * Sized Q2 window (FB+VGA+text).  Use phys stack for span.
	 * Q0 devices mapped for hard-P UART.
	 */
	if (tp_soft_setup_user_quarter(mm, load, stack_phys))
		return;

	/* Restore split notes after memset inside setup. */
	if (saved_flags_split) {
		mm->flags |= TP_SOFT_FL_SPLIT;
		mm->split_phys = split_phys;
		mm->split_la = split_la;
		mm->split_len = split_len;
	}

	/*
	 * Q2: full-quarter identity RWX — FB/VGA/text share Q2 (one region
	 * per LA quarter).  Soft PRIV/RX enforce process isolation on
	 * uaccess; HW RODS stay wide for graphics under P.
	 */
	soft_map_quarter(mm, 2, 1, 1);

	/*
	 * Phase 9 multi-quarter: stack LA in Q3, phys in Q2.
	 * Map Q3 RW no-X via relocate.
	 */
	if ((mm->flags & TP_SOFT_FL_SPLIT) && mm->split_phys && mm->split_la) {
		unsigned qd = (mm->split_la >> 30) & 3u;

		soft_map_remap(mm, qd, mm->split_la, mm->split_phys,
			       mm->split_len, 1, 0);
	} else if (m->start_data && m->start_code) {
		unsigned qt = ((u32)m->start_code >> 30) & 3u;
		unsigned qd = ((u32)m->start_data >> 30) & 3u;
		if (qd != qt && mm->reg[qd].valid) {
			mm->reg[qd].exec = 0;
			mm->reg[qd].write = 1;
		}
		if (mm->reg[qt].valid && qd != qt)
			mm->reg[qt].exec = 1;
	}

	/* Phase 10: soft RX text + private phys span for concurrent soft isol.
	 * Use end_code (not start_data) — RAM bFLT may keep GOT/data after
	 * text but start_data can sit far into writable space on busybox.
	 */
	text_lo = (u32)m->start_code;
	text_hi = (u32)m->end_code;
	if (text_hi <= text_lo)
		text_hi = (u32)m->start_data;
	if (text_hi <= text_lo)
		text_hi = text_lo + 0x1000u;
	mm->text_lo = text_lo;
	mm->text_hi = text_hi;

	/*
	 * The private span is the block binfmt_flat allocated: text, data,
	 * bss, brk room up to context.end_brk, then the stack, whose size
	 * comes from the bFLT header.
	 */
	priv_lo = load & ~0xfffu;
	if ((u32)m->context.end_brk > priv_lo)
		priv_hi = (u32)m->context.end_brk;
	else
		priv_hi = load + 0x40000u;
	/*
	 * Stack top (regs->wptr) must be inside [priv_lo, priv_hi).
	 * stack_phys above is near-top (top-0x1000); use real top + headroom.
	 */
	{
		u32 stack_top_phys = stack_top;

		if (((stack_top >> 30) & 3u) == 3u && split_phys && split_la &&
		    split_len && stack_top >= split_la &&
		    stack_top < split_la + split_len)
			stack_top_phys = split_phys + (stack_top - split_la);
		else if (((stack_top >> 30) & 3u) == 3u)
			stack_top_phys = stack_top - 0x40000000u;
		if (stack_top_phys + 0x20u > priv_hi)
			priv_hi = stack_top_phys + 0x20u;
		if (stack_top_phys < priv_lo)
			priv_lo = stack_top_phys & ~0xfffu;
	}
	mm->priv_lo = priv_lo;
	mm->priv_hi = priv_hi;
	mm->flags |= TP_SOFT_FL_PRIV;

	/* Hard-P requires ACTIVE maps for goprot RODS. */
	if (!tp_soft_enforce && !tp_hard_p)
		mm->flags &= ~TP_SOFT_FL_ACTIVE;
	else
		mm->flags |= TP_SOFT_FL_ACTIVE | TP_SOFT_FL_SYSCALL;

	/* Cache RODS words for hard-path / scheduler re-install. */
	tp_soft_fill_rods(mm, mm->rods_cache);

	/* The gate may have been published before cmdline parsing. */
	*(volatile u32 *)TP_TRAP_ABI_ADDR = tp_hard_p ? TP_TRAP_ABI_V1 : 0;

	pr_info_once("tp32x_prot: soft-mm per-task load=%08x stack=%08x "
		     "enforce=%d hard_p=%d flags=%x "
		     "text=%08x..%08x priv=%08x..%08x "
		     "rods=%08x/%08x/%08x/%08x\n",
		     load, stack_base, tp_soft_enforce, tp_hard_p, mm->flags,
		     mm->text_lo, mm->text_hi, mm->priv_lo, mm->priv_hi,
		     mm->rods_cache[0], mm->rods_cache[1],
		     mm->rods_cache[2], mm->rods_cache[3]);
}

/* ------------------------------------------------------------------
 * Boot: parse "tp_enforce" from the (static) command line
 * ------------------------------------------------------------------ */
extern char boot_command_line[];

static int __init tp_prot_parse_cmdline(void)
{
	if (strstr(boot_command_line, "tp_enforce"))
		tp_soft_enforce = 1;
	if (strstr(boot_command_line, "tp_hard_p")) {
		tp_hard_p = 1;
		/* Hard P needs ACTIVE region maps for RODS. */
		tp_soft_enforce = 1;
	}

	if (tp_hard_p)
		pr_info("tp32x_prot: HARD P-process (goprot enter) + soft ENFORCE\n");
	else if (tp_soft_enforce)
		pr_info("tp32x_prot: soft-mm ENFORCE (no hard goprot enter)\n");
	else
		pr_info("tp32x_prot: soft-mm track-only "
			"(tp_enforce / tp_hard_p)\n");
	return 0;
}
early_initcall(tp_prot_parse_cmdline);

/* ------------------------------------------------------------------
 * Boot selftest — pure soft_ok / RODS without needing a user process
 * ------------------------------------------------------------------ */
static int __init tp_soft_selftest(void)
{
	struct tp_soft_mm mm;
	u32 rods[4];
	u32 pa;
	int fails = 0;

	pr_info("tp32x_prot: selftest begin\n");

	/* Map only quarter 2 (high RAM / FB). */
	memset(&mm, 0, sizeof(mm));
	soft_map_quarter(&mm, 2, 1, 1);
	mm.flags = TP_SOFT_FL_ACTIVE;

	if (!tp_soft_ok(&mm, 0x82001000u, 16, 0)) {
		pr_err("tp32x_prot: FAIL in-quarter read should pass\n");
		fails++;
	}
	if (!tp_soft_ok(&mm, (u32)TRANSPUTER_FB_BASE, 4, 1)) {
		pr_err("tp32x_prot: FAIL FB write should pass\n");
		fails++;
	}
	if (tp_soft_ok(&mm, 0x10000000u, 4, 0)) {
		pr_err("tp32x_prot: FAIL low UART should reject\n");
		fails++;
	}
	/* NULL pointer / page 0 must fail under enforce (Q0 unmapped). */
	if (tp_soft_ok(&mm, 0u, 4, 0)) {
		pr_err("tp32x_prot: FAIL NULL should reject\n");
		fails++;
	}
	if (tp_soft_ok(&mm, 0xC0000000u, 4, 0)) {
		pr_err("tp32x_prot: FAIL quarter 3 should reject\n");
		fails++;
	}

	/* RODS: quarter-2 identity must be non-null with W|X */
	tp_soft_fill_rods(&mm, rods);
	if (rods[2] == TP_NULL_REGION) {
		pr_err("tp32x_prot: FAIL rods[2] null\n");
		fails++;
	}
	if (!(rods[2] & 1u) || !(rods[2] & 2u)) {
		pr_err("tp32x_prot: FAIL rods[2]=%08x missing W/X\n", rods[2]);
		fails++;
	}
	if (rods[0] != TP_NULL_REGION || rods[1] != TP_NULL_REGION ||
	    rods[3] != TP_NULL_REGION) {
		pr_err("tp32x_prot: FAIL unused rods not null\n");
		fails++;
	}

	soft_map_remap(&mm, 3, 0xc0000000u, 0x80000000u, 1u << 30, 1, 0);
	if (!tp_soft_ok(&mm, 0xffffffe8u, 24, 0) ||
	    tp_soft_ok(&mm, 0xffffffecu, 24, 0) ||
	    tp_la_to_phys(&mm, 0xffffffe8u) != 0xbfffffe8u) {
		pr_err("tp32x_prot: FAIL Q3 frame bounds or translation\n");
		fails++;
	}

	/* Inactive mm = full access (compat) */
	mm.flags = 0;
	if (!tp_soft_ok(&mm, 0x10000000u, 4, 0)) {
		pr_err("tp32x_prot: FAIL inactive should allow all\n");
		fails++;
	}

	(void)pa;
	/*
	 * Do not call tp_enter_user_p here: with TP_PROTECT=1, goprot jumps
	 * to the PDS entry and only returns on trap.  A random entry hangs
	 * late_initcall.  Hard enter is covered by emulator tests/hello_p.btl and
	 * live spawn via tp_enter_userspace().
	 */
	if (fails == 0)
		pr_info("tp32x_prot: selftest PASS\n");
	else
		pr_err("tp32x_prot: selftest FAIL (%d)\n", fails);

	return 0;
}
late_initcall(tp_soft_selftest);

EXPORT_SYMBOL(tp_task_soft_mm);
EXPORT_SYMBOL(tp_soft_setup_user);
EXPORT_SYMBOL(tp_enter_user_p);
EXPORT_SYMBOL(tp_run_user_until_exit);
