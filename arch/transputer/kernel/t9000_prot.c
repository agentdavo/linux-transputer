// SPDX-License-Identifier: GPL-2.0
/*
 * Phase 4–6: Linux as T9000 supervisor L-process + soft region MMU.
 *
 * Soft path: per-task thread.soft_mm shadows the 4 T9000 regions; when
 * T9K_SOFT_FL_ACTIVE is set, access_ok enforces them (opt-in via
 * kernel cmdline "t9k_enforce" or compile T9K_SOFT_ENFORCE_DEFAULT).
 *
 * Hard path (T4 emulated goprot/syscall): build PDS/RODS and call
 * t9k_asm_goprot(); PDS.cntl low 16 holds trap cause (t4/t9000_protect.c).
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <asm/t9000_softprot.h>
#include <asm/ptrace.h>
#include <asm/simplefb.h>

extern void do_exit(long code);

/* Match t4/t9000_protect.h trap causes */
#define T9K_TRAP_SYSCALL	3

#define T9K_PS_CNTL	0
#define T9K_PS_EPTR	1
#define T9K_PS_SWPTR	4
#define T9K_PS_SIPTR	5
#define T9K_PS_SAREG	6
#define T9K_PS_SBREG	7
#define T9K_PS_SCREG	8
#define T9K_PDS_WORDS	11
#define T9K_NULL_REGION	0x80000000u

/* Provided by t9000_goprot.S — real T9000 encodings (60 FA / 60 F8) */
void t9k_asm_goprot(unsigned long pds_phys, unsigned long rods_phys);
void t9k_asm_syscall(void);

/*
 * PDS/RODS staging for hard goprot.  Use static (not percpu) buffers —
 * this_cpu_ptr has been unreliable under TCC for small arrays, and
 * goprot once saw rods_phys=0.
 */
static u32 soft_pds_buf[T9K_PDS_WORDS];
static u32 soft_rods_buf[4];

/*
 * Soft-mm enforcement: 0 = track only (identity access_ok), 1 = ACTIVE.
 * Hard P-process: enter user via goprot (T4 emulated) when ACTIVE.
 * Cmdline: "t9k_enforce" and/or "t9k_hard_p" (hard implies enforce).
 */
#ifndef T9K_SOFT_ENFORCE_DEFAULT
#define T9K_SOFT_ENFORCE_DEFAULT	0
#endif
#ifndef T9K_HARD_P_DEFAULT
#define T9K_HARD_P_DEFAULT	0
#endif
static int t9k_soft_enforce = T9K_SOFT_ENFORCE_DEFAULT;
static int t9k_hard_p = T9K_HARD_P_DEFAULT;

/* Classic L-mode enter (switch.S) — used when hard P is off or goprot no-ops. */
extern void __transputer_enter_exec(unsigned long entry, unsigned long wptr);

struct t9k_soft_mm *t9k_task_soft_mm(struct task_struct *t)
{
	if (!t)
		t = current;
	return &t->thread.soft_mm;
}

int t9k_soft_enforce_enabled(void)
{
	return t9k_soft_enforce;
}
EXPORT_SYMBOL(t9k_soft_enforce_enabled);

int t9k_hard_p_enabled(void)
{
	return t9k_hard_p;
}
EXPORT_SYMBOL(t9k_hard_p_enabled);

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
 * Safe first-step enforce for TransputerOS: all bFLT, FB, SHM, and VGA
 * control live in quarter 2 (0x80000000–0xBFFFFFFF).  Quarters 0/1/3
 * stay unmapped so stray low/high user pointers fail access_ok.
 */
static void soft_map_quarter(struct t9k_soft_mm *mm, unsigned q,
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
int t9k_soft_setup_user(struct t9k_soft_mm *mm, u32 load_addr,
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

	mm->flags = T9K_SOFT_FL_ACTIVE | T9K_SOFT_FL_SYSCALL;
	return 0;
}

/*
 * Map a power-of-two window inside quarter @q covering [cover_lo, cover_hi).
 * T9000: one region per LA quarter — base is aligned down to @sz within the
 * quarter (bottom-style identity for that window).
 */
static void soft_map_quarter_window(struct t9k_soft_mm *mm, unsigned q,
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
 * Used for Q3 data with Q2 physical pages (T9000 region relocate).
 */
static void soft_map_remap(struct t9k_soft_mm *mm, unsigned q,
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
int t9k_soft_setup_user_quarter(struct t9k_soft_mm *mm, u32 load_addr,
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

	/* Shared TransputerOS surfaces (same quarter as typical bFLT). */
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

	mm->flags = T9K_SOFT_FL_ACTIVE | T9K_SOFT_FL_SYSCALL;
	return 0;
}
EXPORT_SYMBOL(t9k_soft_setup_user_quarter);

/* Phase 9 notes kept outside soft_mm so setup memset cannot clobber them. */
static u32 t9k_split_data_phys;
static u32 t9k_split_data_la;
static u32 t9k_split_data_len;

/*
 * binfmt_flat records a multi-quarter load (data phys in Q2, LA in Q3).
 * Do NOT set ACTIVE here — reloc/GOT still touch Q2 phys via put_user.
 */
void t9k_note_split_load(u32 data_phys, u32 data_la, u32 data_map_len)
{
	struct t9k_soft_mm *mm = t9k_task_soft_mm(current);

	if (!data_phys || !data_la || !data_map_len)
		return;
	t9k_split_data_phys = data_phys;
	t9k_split_data_la = data_la;
	t9k_split_data_len = data_map_len;
	if (mm)
		mm->flags |= T9K_SOFT_FL_SPLIT;
}
EXPORT_SYMBOL(t9k_note_split_load);

/* Clear soft-mm and force L (spawn/task teardown). */
void t9k_soft_mm_clear(struct task_struct *t)
{
	struct t9k_soft_mm *mm = t9k_task_soft_mm(t);

	if (mm)
		memset(mm, 0, sizeof(*mm));
	/* Split notes are process-scoped; drop on teardown. */
	if (!t || t == current) {
		t9k_split_data_phys = 0;
		t9k_split_data_la = 0;
		t9k_split_data_len = 0;
	}
	/* T4 leave_p MMIO — safe no-op if not on T4 */
	*(volatile unsigned int *)0xA000001Cu = 0;
}
EXPORT_SYMBOL(t9k_soft_mm_clear);

void t9k_soft_fill_rods(const struct t9k_soft_mm *mm, u32 *rods)
{
	int i;
	if (!rods)
		return;
	for (i = 0; i < 4; i++) {
		if (mm && mm->reg[i].valid)
			rods[i] = make_desc(mm->reg[i].phys_base,
					    mm->reg[i].size,
					    mm->reg[i].write,
					    mm->reg[i].exec);
		else
			rods[i] = T9K_NULL_REGION;
	}
}
EXPORT_SYMBOL(t9k_soft_fill_rods);

static void fill_pds(u32 *pds, u32 entry, u32 wsp)
{
	int i;
	for (i = 0; i < T9K_PDS_WORDS; i++)
		pds[i] = 0;
	/* Explicit indices — match t4/t9000_protect.h */
	pds[4] = wsp;	/* SWPTR */
	pds[5] = entry;	/* SIPTR */
}

/*
 * Soft enter: mark ACTIVE and leave regs for classic return-to-user.
 * Hard enter: goprot once; returns trap cause (3 = syscall).
 */
int t9k_enter_user_p(struct t9k_soft_mm *mm, u32 entry, u32 wsp)
{
	u32 *pds, *rods;
	u32 cause;

	if (!mm || !(mm->flags & T9K_SOFT_FL_ACTIVE))
		return -EINVAL;

	pds = soft_pds_buf;
	rods = soft_rods_buf;
	t9k_soft_fill_rods(mm, rods);
	fill_pds(pds, entry, wsp);
	mm->pds_phys = (u32)(unsigned long)pds;

	/*
	 * Hard path: real goprot (T4 emulated).  On return, PDS.cntl low 16
	 * is trap cause.  Without T4_PROTECT the insn may be a no-op (cause 0).
	 */
	t9k_asm_goprot((unsigned long)pds, (unsigned long)rods);

	cause = pds[T9K_PS_CNTL] & 0xffffu;
	if (cause == 0) {
		pr_info_once("t9000_prot: soft P active entry=%08x wsp=%08x\n",
			     entry, wsp);
		return 0;
	}
	return (int)cause;
}

extern long __transputer_syscall(long, long, long, long, long, long, long);

/*
 * Run a user P-session: goprot → user until trap.
 *
 * On T4, P-trap longjmps to the emulator mainloop with IPtr restored to the
 * instruction after goprot, so t9k_asm_goprot() returns to us with PDS.cntl
 * holding the trap cause.
 *
 * cause 0  → goprot was a no-op (T4_PROTECT=0): caller should classic-enter.
 * cause 3  → syscall trap: dispatch, write return to sareg, re-goprot.
 * other    → access/priv/etc: return cause to caller.
 *
 * Do NOT refill PDS on restart — trap already updated siptr/sareg/swptr.
 */
int t9k_run_user_until_exit(struct t9k_soft_mm *mm, u32 entry, u32 wsp)
{
	u32 *pds, *rods;
	int cause;
	int restarts = 0;

	if (!mm || !(mm->flags & T9K_SOFT_FL_ACTIVE))
		return -EINVAL;

	pds = soft_pds_buf;
	rods = soft_rods_buf;
	t9k_soft_fill_rods(mm, rods);
	fill_pds(pds, entry, wsp);
	pds[0] = 0; /* CNTL */
	mm->pds_phys = (u32)(unsigned long)pds;
	/* Leave P if a prior C-call left is_p set — goprot must start from L. */
	*(volatile unsigned int *)0xA000001Cu = 0;

	for (;;) {
		t9k_asm_goprot((unsigned long)pds, (unsigned long)rods);
		cause = (int)(pds[T9K_PS_CNTL] & 0xffffu);

		if (cause == 0)
			return 0; /* soft no-op — fall back to L enter */
		if (cause == T9K_TRAP_SYSCALL) {
			long nr = (long)pds[T9K_PS_SAREG];
			long a1 = (long)pds[T9K_PS_SBREG];
			long a2 = (long)pds[T9K_PS_SCREG];
			long a3 = 0, a4 = 0, a5 = 0, a6 = 0;
			long ret;
			u32 wsp = pds[T9K_PS_SWPTR];

			/*
			 * Trap ABI (TP_USE_T9K_SYSCALL / pure asm):
			 *   A = nr, B = a1, C = a2
			 *   User WPtr words 1..4 = a3..a6 (optional)
			 *
			 * C-call gate apps never take this path while they
			 * CALL the gate under P without the syscall insn.
			 *
			 * If A does not look like a syscall number, recover
			 * full frame from WPtr[1..7] (TCC-style CALL frame).
			 */
			if (nr <= 0 || nr > 4096) {
				if (wsp) {
					nr = *(long *)(unsigned long)(wsp + 4);
					a1 = *(long *)(unsigned long)(wsp + 8);
					a2 = *(long *)(unsigned long)(wsp + 12);
					a3 = *(long *)(unsigned long)(wsp + 16);
					a4 = *(long *)(unsigned long)(wsp + 20);
					a5 = *(long *)(unsigned long)(wsp + 24);
					a6 = *(long *)(unsigned long)(wsp + 28);
				}
			} else if (wsp) {
				a3 = *(long *)(unsigned long)(wsp + 4);
				a4 = *(long *)(unsigned long)(wsp + 8);
				a5 = *(long *)(unsigned long)(wsp + 12);
				a6 = *(long *)(unsigned long)(wsp + 16);
			}

			ret = __transputer_syscall(nr, a1, a2, a3, a4, a5, a6);
			pds[T9K_PS_SAREG] = (u32)ret;
			pds[T9K_PS_CNTL] = 0; /* clear for next re-enter */
			if (++restarts > 100000)
				return -EAGAIN;
			continue;
		}
		/* PRIV (stopp/exit), ACCESS, etc. */
		return cause;
	}
}

/*
 * Unified userspace entry (Phase 7).
 *
 * Prefer hard P-process via goprot when t9k_hard_p && ACTIVE.  If goprot
 * is a no-op (cause 0), fall through to classic L-mode enter_exec.
 * Never returns on a successful classic enter.
 */
void t9k_enter_userspace(unsigned long entry, unsigned long wptr)
{
	struct t9k_soft_mm *mm = t9k_task_soft_mm(current);
	int cause;

	if (t9k_hard_p && mm && (mm->flags & T9K_SOFT_FL_ACTIVE)) {
		static unsigned long p_enters;

		p_enters++;
		if (p_enters == 1 || (p_enters & 0x3f) == 0)
			pr_info("t9000_prot: P-enter #%lu entry=%08lx wsp=%08lx\n",
				p_enters, entry, wptr);

		cause = t9k_run_user_until_exit(mm, (u32)entry, (u32)wptr);
		if (cause == 0) {
			/* T4_PROTECT off or goprot soft — classic enter */
			pr_info_once("t9000_prot: hard-P goprot no-op → classic enter\n");
			__transputer_enter_exec(entry, wptr);
			return;
		}
		/*
		 * Session ended by non-syscall trap.  If spawn longjmp already
		 * left this stack, we never get here.  Otherwise exit the task.
		 */
		pr_info("t9000_prot: P-session end cause=%d entry=%08lx\n",
			cause, entry);
		/* Ensure L before task teardown */
		*(volatile unsigned int *)0xA000001Cu = 0;
		do_exit(cause);
	}

	__transputer_enter_exec(entry, wptr);
}
EXPORT_SYMBOL(t9k_enter_userspace);

/* Called from start_thread / transputer_flat_finish after regs are primed */
void t9k_on_exec_finish(struct pt_regs *regs)
{
	struct t9k_soft_mm *mm = t9k_task_soft_mm(current);
	struct mm_struct *m = current->mm;
	u32 load, stack_base, stack_top;
	u32 rods[4];
	u32 saved_flags_split;

	if (!m || !regs || !mm)
		return;

	saved_flags_split = mm->flags & T9K_SOFT_FL_SPLIT;

	load = (u32)m->start_code;
	if (!load)
		load = (u32)regs->iptr;

	stack_top = (u32)regs->wptr;
	/* Stack grows down; base for quarter select is near top. */
	stack_base = (stack_top > 0x1000) ? (stack_top - 0x1000) : stack_top;

	/*
	 * Sized Q2 window (FB+VGA+text).  Q0 stays unmapped → NULL / low
	 * pointers fail access_ok under enforce.
	 */
	if (t9k_soft_setup_user_quarter(mm, load, stack_base))
		return;

	if (saved_flags_split)
		mm->flags |= T9K_SOFT_FL_SPLIT;

	/*
	 * Phase 9 multi-quarter: data LA in Q3, phys in Q2.
	 * Map Q3 RW no-X via relocate.  Keep Q2 writable so FB/SHM/VGA
	 * (same quarter as text) still work under P.
	 */
	if ((mm->flags & T9K_SOFT_FL_SPLIT) && t9k_split_data_phys &&
	    t9k_split_data_la) {
		unsigned qd = (t9k_split_data_la >> 30) & 3u;
		unsigned qt = (load >> 30) & 3u;

		soft_map_remap(mm, qd, t9k_split_data_la, t9k_split_data_phys,
			       t9k_split_data_len, 1, 0);
		if (qt != qd && mm->reg[qt].valid)
			mm->reg[qt].exec = 1;
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

	/* Hard-P requires ACTIVE maps for goprot RODS. */
	if (!t9k_soft_enforce && !t9k_hard_p)
		mm->flags &= ~T9K_SOFT_FL_ACTIVE;
	else
		mm->flags |= T9K_SOFT_FL_ACTIVE | T9K_SOFT_FL_SYSCALL;

	/* Cache RODS words for hard-path / debug. */
	t9k_soft_fill_rods(mm, rods);
	mm->pds_phys = 0;

	pr_info_once("t9000_prot: soft-mm per-task load=%08x stack=%08x "
		     "enforce=%d hard_p=%d flags=%x "
		     "rods=%08x/%08x/%08x/%08x\n",
		     load, stack_base, t9k_soft_enforce, t9k_hard_p, mm->flags,
		     rods[0], rods[1], rods[2], rods[3]);
}

/* ------------------------------------------------------------------
 * Boot: parse "t9k_enforce" from the (static) command line
 * ------------------------------------------------------------------ */
extern char boot_command_line[];

static int __init t9k_prot_parse_cmdline(void)
{
	if (strstr(boot_command_line, "t9k_enforce"))
		t9k_soft_enforce = 1;
	if (strstr(boot_command_line, "t9k_hard_p")) {
		t9k_hard_p = 1;
		/* Hard P needs ACTIVE region maps for RODS. */
		t9k_soft_enforce = 1;
	}

	if (t9k_hard_p)
		pr_info("t9000_prot: HARD P-process (goprot enter) + soft ENFORCE\n");
	else if (t9k_soft_enforce)
		pr_info("t9000_prot: soft-mm ENFORCE (no hard goprot enter)\n");
	else
		pr_info("t9000_prot: soft-mm track-only "
			"(t9k_enforce / t9k_hard_p)\n");
	return 0;
}
early_initcall(t9k_prot_parse_cmdline);

/* ------------------------------------------------------------------
 * Boot selftest — pure soft_ok / RODS without needing a user process
 * ------------------------------------------------------------------ */
static int __init t9k_soft_selftest(void)
{
	struct t9k_soft_mm mm;
	u32 rods[4];
	u32 pa;
	int fails = 0;

	pr_info("t9000_prot: selftest begin\n");

	/* Map only quarter 2 (high RAM / FB). */
	memset(&mm, 0, sizeof(mm));
	soft_map_quarter(&mm, 2, 1, 1);
	mm.flags = T9K_SOFT_FL_ACTIVE;

	if (!t9k_soft_ok(&mm, 0x82001000u, 16, 0)) {
		pr_err("t9000_prot: FAIL in-quarter read should pass\n");
		fails++;
	}
	if (!t9k_soft_ok(&mm, (u32)TRANSPUTER_FB_BASE, 4, 1)) {
		pr_err("t9000_prot: FAIL FB write should pass\n");
		fails++;
	}
	if (t9k_soft_ok(&mm, 0x10000000u, 4, 0)) {
		pr_err("t9000_prot: FAIL low UART should reject\n");
		fails++;
	}
	/* NULL pointer / page 0 must fail under enforce (Q0 unmapped). */
	if (t9k_soft_ok(&mm, 0u, 4, 0)) {
		pr_err("t9000_prot: FAIL NULL should reject\n");
		fails++;
	}
	if (t9k_soft_ok(&mm, 0xC0000000u, 4, 0)) {
		pr_err("t9000_prot: FAIL quarter 3 should reject\n");
		fails++;
	}

	/* RODS: quarter-2 identity must be non-null with W|X */
	t9k_soft_fill_rods(&mm, rods);
	if (rods[2] == T9K_NULL_REGION) {
		pr_err("t9000_prot: FAIL rods[2] null\n");
		fails++;
	}
	if (!(rods[2] & 1u) || !(rods[2] & 2u)) {
		pr_err("t9000_prot: FAIL rods[2]=%08x missing W/X\n", rods[2]);
		fails++;
	}
	if (rods[0] != T9K_NULL_REGION || rods[1] != T9K_NULL_REGION ||
	    rods[3] != T9K_NULL_REGION) {
		pr_err("t9000_prot: FAIL unused rods not null\n");
		fails++;
	}

	/* Inactive mm = full access (compat) */
	mm.flags = 0;
	if (!t9k_soft_ok(&mm, 0x10000000u, 4, 0)) {
		pr_err("t9000_prot: FAIL inactive should allow all\n");
		fails++;
	}

	(void)pa;
	/*
	 * Do not call t9k_enter_user_p here: with T4_PROTECT=1, goprot jumps
	 * to the PDS entry and only returns on trap.  A random entry hangs
	 * late_initcall.  Hard enter is covered by T4 tests/hello_p.btl and
	 * live spawn via t9k_enter_userspace().
	 */
	if (fails == 0)
		pr_info("t9000_prot: selftest PASS\n");
	else
		pr_err("t9000_prot: selftest FAIL (%d)\n", fails);

	return 0;
}
late_initcall(t9k_soft_selftest);

EXPORT_SYMBOL(t9k_task_soft_mm);
EXPORT_SYMBOL(t9k_soft_setup_user);
EXPORT_SYMBOL(t9k_enter_user_p);
EXPORT_SYMBOL(t9k_run_user_until_exit);
