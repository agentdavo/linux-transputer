// SPDX-License-Identifier: GPL-2.0
/*
 * PGM1 paging for Transputer Linux (emulator PAGING.md).
 *
 * The kernel runs as the supervisor L-process with physical addresses:
 * low memory at PAGE_OFFSET is physical RAM, MMIO is physical, and only
 * the window at PGM1_KWIN_BASE (vmalloc, ioremap and the frame slots) is
 * translated, through swapper_pg_dir.  User processes are P-processes
 * translated by their mm's page tables.  So the kernel never dereferences
 * a user address: uaccess walks current->mm and copies through the
 * physical (or window-mapped) frame, faulting pages in with
 * fixup_user_fault() like a hardware fault would.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/futex.h>
#include <asm/cmpxchg.h>
#include <asm/pgm1.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/tp_softprot.h>
#ifdef CONFIG_HIGHMEM
#include <linux/highmem.h>
#endif

extern int tp_asm_goprot(u32 *pds, u32 *rods, struct tp_p_extra *extra);

static bool pgm1_on;
/* Frame slots in use, per CPU (the slots of CPU n start at slot 16n) */
static DEFINE_PER_CPU(unsigned int, pgm1_slot_top);

bool pgm1_present(void)
{
	return pgm1_on;
}

static pte_t *pgm1_kpte(unsigned long va)
{
	pgd_t *pgd = swapper_pg_dir + pgd_index(va);

	return (pte_t *)__va(pgd_val(*pgd) & PAGE_MASK) + pte_index(va);
}

/*
 * Every leaf table the window can use exists from boot, so vmalloc,
 * ioremap and the frame slots never allocate a kernel table, and the
 * single kernel directory needs no per-process copies: the user
 * directory covers user addresses only.
 */
/* Each CPU has its own control page: point it at the kernel tables
 * and the window, then turn on user and window translation. */
static void pgm1_cpu_on(void)
{
	pgm1_write(PGM1_KROOT, __pa(swapper_pg_dir));
	pgm1_write(PGM1_KWIN, PGM1_KWIN_BASE);
	pgm1_write(PGM1_CTRL, PGM1_CTRL_USER | PGM1_CTRL_KWIN);
	local_flush_tlb_all();
}

#ifdef CONFIG_SMP
/* A secondary CPU, before it runs anything that uses the window. */
void pgm1_secondary_init(void)
{
	if (pgm1_on)
		pgm1_cpu_on();
}
#endif

void __init pgm1_paging_init(void)
{
	unsigned long va;
	unsigned int tables = 0;

	if (pgm1_read(PGM1_ID) != PGM1_ID_VALUE) {
		pr_warn("pgm1: control page absent (id %08x), paging off\n",
			pgm1_read(PGM1_ID));
		return;
	}
	for (va = PGM1_KWIN_BASE; va >= PGM1_KWIN_BASE; va += PGDIR_SIZE) {
		pgd_t *pgd = swapper_pg_dir + pgd_index(va);

		if (!pgd_val(*pgd)) {
			pte_t *pte = memblock_alloc(PAGE_SIZE, PAGE_SIZE);

			if (!pte)
				panic("pgm1: no memory for window tables");
			*pgd = __pgd(__pa(pte) | _PAGE_PRESENT);
			tables++;
		}
		if (va + PGDIR_SIZE < va)
			break;
	}
#ifdef CONFIG_HIGHMEM
	pkmap_page_table = pgm1_kpte(PKMAP_BASE);
#endif
	pgm1_cpu_on();
	pgm1_on = true;
	pr_info("pgm1: PA bits %u, RAM0 %08x+%08x, high bank %08x%08x+%08x%08x, "
		"window %08lx.. (%u tables), kroot %08lx\n",
		pgm1_read(PGM1_PA_BITS), pgm1_read(PGM1_RAM0_BASE), pgm1_read(PGM1_RAM0_SIZE),
		pgm1_read(PGM1_HIGH_BASE_HI), pgm1_read(PGM1_HIGH_BASE_LO),
		pgm1_read(PGM1_HIGH_SIZE_HI), pgm1_read(PGM1_HIGH_SIZE_LO),
		PGM1_KWIN_BASE, tables, (unsigned long)__pa(swapper_pg_dir));
}

/* The physical address behind a supervisor window address, or ~0 when
 * nothing is mapped there (SMP atomics, kernel/smp_locks.c). */
u64 pgm1_window_pa(unsigned long va)
{
	pgd_t *pgd = swapper_pg_dir + pgd_index(va);
	pte_t pte;

	if ((pgd_val(*pgd) & ~PAGE_MASK) != _PAGE_PRESENT)
		return ~0ULL;
	pte = *((pte_t *)__va(pgd_val(*pgd) & PAGE_MASK) + pte_index(va));
	if (!(pte_val(pte) & _PAGE_PRESENT))
		return ~0ULL;
	return ((u64)pte_pfn(pte) << PAGE_SHIFT) | (va & ~PAGE_MASK);
}

/* Frame slots: a small stack of window pages, for frames with no
 * permanent kernel address (above 4 GiB, or outside low memory). */
static unsigned long pgm1_slot_va(unsigned int cpu, unsigned int n)
{
	return PGM1_SLOT_BASE + (((unsigned long)cpu * PGM1_NR_SLOTS + n) << PAGE_SHIFT);
}

/* The mapping is this CPU's alone, so preemption stays off until
 * pgm1_unmap_frame and the flushes are local. */
void *pgm1_map_frame(unsigned long pfn)
{
	unsigned int *top;
	unsigned long va;

	preempt_disable();
	top = this_cpu_ptr(&pgm1_slot_top);
	BUG_ON(*top >= PGM1_NR_SLOTS);
	va = pgm1_slot_va(smp_processor_id(), (*top)++);
	set_pte(pgm1_kpte(va), pfn_pte(pfn, PAGE_KERNEL));
	local_flush_tlb_one(va);
	return (void *)va;
}
EXPORT_SYMBOL(pgm1_map_frame);

void pgm1_unmap_frame(void *p)
{
	unsigned long va = (unsigned long)p & PAGE_MASK;

	unsigned int *top = this_cpu_ptr(&pgm1_slot_top);

	BUG_ON(!*top);
	(*top)--;
	WARN_ON(va != pgm1_slot_va(smp_processor_id(), *top));
	set_pte(pgm1_kpte(va), __pte(0));
	local_flush_tlb_one(va);
	preempt_enable();
}

#ifdef CONFIG_SMP
/* Futex words: the leaf must be present and writable (the generic code
 * faults the page in and retries on -EFAULT).  Preemption stays off
 * from the walk to the last access, as in pgm1_copy_user. */
static u32 *pgm1_futex_get(u32 __user *uaddr, bool *slot)
{
	unsigned long la = (unsigned long)uaddr, pfn;
	unsigned int w = (la & ~PAGE_MASK) >> 2;

	if ((la & 3) || !current->mm || la >= TASK_SIZE)
		return NULL;
	preempt_disable();
	if (pgm1_user_pfn(current->mm, la, 1, &pfn)) {
		preempt_enable();
		return NULL;
	}
	*slot = !(pfn >= ARCH_PFN_OFFSET && pfn < max_low_pfn);
	if (*slot)
		return (u32 *)pgm1_map_frame(pfn) + w;
	return (u32 *)__va(pfn << PAGE_SHIFT) + w;
}

static void pgm1_futex_put(u32 *p, bool slot)
{
	if (slot)
		pgm1_unmap_frame(p);
	preempt_enable();
}

int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 oldval, u32 newval)
{
	bool slot;
	u32 *p = pgm1_futex_get(uaddr, &slot);

	if (!p)
		return -EFAULT;
	*uval = arch_cmpxchg(p, oldval, newval);
	pgm1_futex_put(p, slot);
	return 0;
}

int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr)
{
	bool slot;
	u32 *p, old, new, seen;
	int ret = 0;

	if (op < FUTEX_OP_SET || op > FUTEX_OP_XOR)
		return -ENOSYS;
	p = pgm1_futex_get(uaddr, &slot);
	if (!p)
		return -EFAULT;
	old = *(volatile u32 *)p;
	for (;;) {
		switch (op) {
		case FUTEX_OP_SET: new = oparg; break;
		case FUTEX_OP_ADD: new = old + oparg; break;
		case FUTEX_OP_OR: new = old | oparg; break;
		case FUTEX_OP_ANDN: new = old & ~oparg; break;
		default: new = old ^ oparg; break;
		}
		seen = arch_cmpxchg(p, old, new);
		if (seen == old)
			break;
		old = seen;
	}
	*oval = old;
	pgm1_futex_put(p, slot);
	return ret;
}

/* TLB shootdown: the TLBs are untagged, so every online CPU flushes,
 * whichever mm it runs.  IPIs are polled (kernel/smp.c), so a CPU
 * answers at its next safe point or P trap. */
struct pgm1_flush_args {
	unsigned long start, end;
};

static void pgm1_ipi_flush_all(void *unused)
{
	local_flush_tlb_all();
}

static void pgm1_ipi_flush_range(void *p)
{
	struct pgm1_flush_args *a = p;

	local_flush_tlb_range(a->start, a->end);
}

void flush_tlb_all(void)
{
	on_each_cpu(pgm1_ipi_flush_all, NULL, 1);
}
EXPORT_SYMBOL(flush_tlb_all);

void flush_tlb_range_va(unsigned long start, unsigned long end)
{
	struct pgm1_flush_args a = { start, end };

	on_each_cpu(pgm1_ipi_flush_range, &a, 1);
}
EXPORT_SYMBOL(flush_tlb_range_va);

void flush_tlb_one(unsigned long addr)
{
	flush_tlb_range_va(addr, addr + PAGE_SIZE);
}
EXPORT_SYMBOL(flush_tlb_one);
#endif
EXPORT_SYMBOL(pgm1_unmap_frame);

/* ---- uaccess ----------------------------------------------------------- */

/* The leaf for a user address, if it permits the access. */
static int pgm1_user_pfn(struct mm_struct *mm, unsigned long addr, int write,
			 unsigned long *pfn)
{
	pgd_t *pgd = pgd_offset(mm, addr);
	pte_t pte;

	if ((pgd_val(*pgd) & ~PAGE_MASK) != _PAGE_PRESENT)
		return -1;
	pte = *((pte_t *)__va(pgd_val(*pgd) & PAGE_MASK) + pte_index(addr));
	if (!(pte_val(pte) & _PAGE_PRESENT) ||
	    !(pte_val(pte) & (write ? _PAGE_WRITE : _PAGE_READ)))
		return -1;
	*pfn = pte_pfn(pte);
	return 0;
}

static int pgm1_fault_in(struct mm_struct *mm, unsigned long addr, int write)
{
	int ret;

	if (mmap_read_lock_killable(mm))
		return -EINTR;
	ret = fixup_user_fault(mm, addr, write ? FAULT_FLAG_WRITE : 0, NULL);
	mmap_read_unlock(mm);
	return ret;
}

/* Copy n bytes between kbuf and user uaddr (to user when write).
 * Returns the bytes not copied. */
static unsigned long pgm1_copy_user(void *kbuf, unsigned long uaddr,
				    unsigned long n, int write)
{
	struct mm_struct *mm = current->mm;

	while (n) {
		unsigned long off = uaddr & ~PAGE_MASK;
		unsigned long chunk = min(n, PAGE_SIZE - off);
		unsigned long pfn;
		void *page;
		bool slot = false;

		if (!mm || uaddr >= TASK_SIZE || chunk > TASK_SIZE - uaddr)
			break;
		preempt_disable();
		if (pgm1_user_pfn(mm, uaddr, write, &pfn)) {
			preempt_enable();
			if (pagefault_disabled() || pgm1_fault_in(mm, uaddr, write))
				break;
			continue;
		}
		if (pfn >= ARCH_PFN_OFFSET && pfn < max_low_pfn) {
			page = __va(pfn << PAGE_SHIFT);
		} else {
			page = pgm1_map_frame(pfn);
			slot = true;
		}
		if (write)
			memcpy(page + off, kbuf, chunk);
		else
			memcpy(kbuf, page + off, chunk);
		if (slot)
			pgm1_unmap_frame(page);
		preempt_enable();
		kbuf += chunk;
		uaddr += chunk;
		n -= chunk;
	}
	return n;
}

unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return pgm1_copy_user(to, (unsigned long)from, n, 0);
}
EXPORT_SYMBOL(raw_copy_from_user);

unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	return pgm1_copy_user((void *)from, (unsigned long)to, n, 1);
}
EXPORT_SYMBOL(raw_copy_to_user);

unsigned long pgm1_clear_user(void __user *to, unsigned long n)
{
	static const u8 zero[64];

	while (n) {
		unsigned long chunk = min(n, (unsigned long)sizeof(zero));

		if (pgm1_copy_user((void *)zero, (unsigned long)to, chunk, 1))
			return n;
		to += chunk;
		n -= chunk;
	}
	return 0;
}
EXPORT_SYMBOL(pgm1_clear_user);

long strncpy_from_user(char *dst, const char __user *src, long count)
{
	long i;

	for (i = 0; i < count; i++) {
		char c;

		if (pgm1_copy_user(&c, (unsigned long)(src + i), 1, 0))
			return -EFAULT;
		dst[i] = c;
		if (!c)
			return i;
	}
	return count;
}
EXPORT_SYMBOL(strncpy_from_user);

long strnlen_user(const char __user *str, long n)
{
	long i;

	for (i = 0; i < n; i++) {
		char c;

		if (pgm1_copy_user(&c, (unsigned long)(str + i), 1, 0))
			return 0;
		if (!c)
			return i + 1;
	}
	return n + 1;
}
EXPORT_SYMBOL(strnlen_user);

/* ---- P-process faults ---------------------------------------------------- */

int pgm1_handle_user_fault(unsigned long va, unsigned int kind)
{
	struct mm_struct *mm = current->mm;
	struct pt_regs regs = { .status = 1 };	/* user mode */
	struct vm_area_struct *vma;
	unsigned int flags = FAULT_FLAG_DEFAULT | FAULT_FLAG_USER;
	vm_flags_t need;
	vm_fault_t fault;

	if (!mm)
		return -EFAULT;
	switch (kind) {
	case 0:
		need = VM_EXEC;
		flags |= FAULT_FLAG_INSTRUCTION;
		break;
	case 1:
		need = VM_READ;
		break;
	case 2:
		need = VM_WRITE;
		flags |= FAULT_FLAG_WRITE;
		break;
	default:
		return -EFAULT;
	}
retry:
	vma = lock_mm_and_find_vma(mm, va, &regs);
	if (!vma)
		return -EFAULT;		/* the lock is already dropped */
	if (!(vma->vm_flags & need)) {
		mmap_read_unlock(mm);
		return -EFAULT;
	}
	fault = handle_mm_fault(vma, va, flags, &regs);
	if (fault_signal_pending(fault, &regs))
		return -EINTR;
	if (fault & VM_FAULT_COMPLETED)
		return 0;
	if (fault & VM_FAULT_RETRY) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}
	mmap_read_unlock(mm);
	return (fault & VM_FAULT_ERROR) ? -EFAULT : 0;
}

int pgm1_run_p(u32 *pds, struct tp_p_extra *extra, unsigned int *faults)
{
	u32 rods[4] = { 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u };

	for (;;) {
		int composed;
		u32 va, acc;

		/* The directory of the mm being run; the write also flushes. */
		pgm1_write(PGM1_UROOT, __pa(current->mm->pgd));
		composed = tp_asm_goprot(pds, rods, extra);
		if ((composed & 0xff) != 0 || (composed >> 8) != PGM1_ET_PAGEFAULT)
			return composed;
		va = pgm1_read(PGM1_FAULT_VA);
		acc = pgm1_read(PGM1_FAULT_ACC);
		if (pgm1_handle_user_fault(va, acc & PGM1_FA_KIND))
			return composed;
		if (faults) {
			(*faults)++;
			pr_info("pgm1: P fault va=%08x kind=%u%s restart=%08x resolved\n",
				va, acc & PGM1_FA_KIND, (acc & PGM1_FA_PROT) ? " (protection)" : "",
				pds[5]);
		}
		/* ps.sIptr is the faulting instruction's first byte: retry it. */
	}
}
