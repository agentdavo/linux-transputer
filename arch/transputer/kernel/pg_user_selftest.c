// SPDX-License-Identifier: GPL-2.0
/*
 * Boot-time acceptance test for PGM1 user paging (emulator PAGING.md).
 *
 * A kernel thread builds a real address space (mm_alloc, vm_mmap'd VMAs,
 * code written through copy_to_user), then runs hand-assembled P-process
 * code in it with goprot.  Its loads and stores fault on absent PTEs;
 * pgm1_run_p() takes each fault to lock_mm_and_find_vma() and
 * handle_mm_fault() and re-executes the instruction from its first byte.
 *
 *  1. ldc D; ldnl 0; adc 1; ldc D; stnl 0; ldc D; ldnl 0; syscall
 *     on an untouched anonymous page: a read fault (zero page), then a
 *     write-protection fault (copy on write).  The result must be 1 and
 *     memory 1: the increment retired once.  A second, uninterrupted run
 *     takes no fault and gives 2.
 *  2. Two pages of a VM_PFNMAP mapping whose fault handler inserts frames
 *     exactly 4 GiB apart (same low 32 bits).  Data written through the
 *     supervisor window is read by the process, its stores are read back
 *     through the window, reads alternate, and an unmap/remap with the
 *     frames swapped must swap what the process sees.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched/mm.h>
#include <linux/uaccess.h>
#include <asm/pgm1.h>
#include <asm/tp_softprot.h>

extern void tp_asm_init_fp(u32 *fp);

#define CODE_VA	0x00400000UL
#define DATA_VA	0x00500000UL
#define WS_VA	0x00600000UL
#define HIGH_VA	0x00700000UL
#define ALLOC_VA	0x00800000UL

static unsigned long high_pfn[2];
static unsigned int high_faults;
static int failures;
static DECLARE_COMPLETION(selftest_done);

static void expect(const char *what, unsigned long got, unsigned long want)
{
	if (got == want)
		return;
	pr_err("pgm1-user: FAIL %s: %08lx, want %08lx\n", what, got, want);
	failures++;
}

/* fn with operand v, prefixed as ISM 4.2 builds it */
static int put(u8 *p, int fn, int v)
{
	int n;

	if (v >= 0 && v < 16) {
		p[0] = fn | v;
		return 1;
	}
	n = v >= 16 ? put(p, 0x20, v >> 4) : put(p, 0x60, (~v) >> 4);
	p[n] = fn | (v & 15);
	return n + 1;
}

static int sysc(u8 *p)
{
	p[0] = 0x60;
	p[1] = 0xF8;
	return 2;
}

static int run(unsigned long entry, u32 *a, u32 *b, unsigned int *faults)
{
	u32 pds[11] = { 0 };
	struct tp_p_extra extra = { };
	int composed;

	tp_asm_init_fp(extra.fp);
	pds[0] = 0x80;			/* ps.Cntl as a fresh process */
	pds[4] = WS_VA + 0xF00;		/* Wptr */
	pds[5] = entry;			/* Iptr */
	*faults = 0;
	composed = pgm1_run_p(pds, &extra, faults);
	*a = pds[6];
	*b = pds[7];
	return composed;
}

static vm_fault_t high_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long idx = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	high_faults++;
	pr_info("pgm1-user: VMA fault va=%08lx -> pfn %07lx pa %010llx\n",
		vmf->address, high_pfn[idx], (unsigned long long)high_pfn[idx] << PAGE_SHIFT);
	return vmf_insert_pfn(vma, vmf->address & PAGE_MASK, high_pfn[idx]);
}

static const struct vm_special_mapping high_spec = {
	.name = "[pgm1-high]",
	.fault = high_fault,
};

static u32 window_word(unsigned long pfn, unsigned int i)
{
	u32 *p = pgm1_map_frame(pfn), v = p[i];

	pgm1_unmap_frame(p);
	return v;
}

static void window_set(unsigned long pfn, unsigned int i, u32 v)
{
	u32 *p = pgm1_map_frame(pfn);

	p[i] = v;
	pgm1_unmap_frame(p);
}

#ifdef CONFIG_HIGHMEM
#include <linux/highmem.h>
#include <linux/gfp.h>
#include <linux/slab.h>

#define BLOCK_ORDER	10			/* 4 MiB */
#define BLOCKS_4G	(1UL << (32 - PAGE_SHIFT - BLOCK_ORDER))

static struct page *alloc_pages_test[4];	/* X, X + 4 GiB, top, NULL */

static const struct vm_special_mapping alloc_spec = {
	.name = "[pgm1-alloc]",
	.pages = alloc_pages_test,
};

static u32 kword(struct page *pg, unsigned int i)
{
	u32 *p = kmap_local_page(pg), v = p[i];

	kunmap_local(p);
	return v;
}

static void kword_set(struct page *pg, unsigned int i, u32 v)
{
	u32 *p = kmap_local_page(pg);

	p[i] = v;
	kunmap_local(p);
}

/* A value no other swept page has: pfn-derived, so frames 4 GiB apart
 * (same low 32 address bits) differ. */
static u32 sweep_value(unsigned long pfn, unsigned int word)
{
	return (u32)(pfn * 2654435761u) ^ (word ? 0xA5A5F00Du : 0x5EE9C0DEu);
}

/*
 * Every 4 MiB of highmem the allocator gave us: write the first and last
 * word of the block's first and last page through kmap_local, then read
 * them all back.  Any two pages sharing storage, any frame the window
 * cannot reach, or a dropped high address bit shows as a mismatch.
 */
static void sweep_blocks(struct list_head *blocks, unsigned long base, unsigned long size)
{
	struct page *pg;
	unsigned long pages = 0, bad = 0, lo = ULONG_MAX, hi = 0;
	int pass, end;

	for (pass = 0; pass < 2; pass++) {
		list_for_each_entry(pg, blocks, lru) {
			for (end = 0; end < 2; end++) {
				struct page *q = end ? pg + (1UL << BLOCK_ORDER) - 1 : pg;
				unsigned long pfn = page_to_pfn(q);
				u32 *w = kmap_local_page(q);

				if (!pass) {
					w[0] = sweep_value(pfn, 0);
					w[1023] = sweep_value(pfn, 1);
					pages++;
					lo = min(lo, pfn);
					hi = max(hi, pfn);
				} else if (w[0] != sweep_value(pfn, 0) ||
					   w[1023] != sweep_value(pfn, 1)) {
					if (!bad)
						pr_err("pgm1-user: sweep mismatch pfn %07lx: %08x %08x\n",
						       pfn, w[0], w[1023]);
					bad++;
				}
				kunmap_local(w);
			}
		}
	}
	pr_info("pgm1-user: sweep wrote and read back %lu pages, pfn %07lx..%07lx "
		"(pa %010llx..%010llx), %lu mismatches\n", pages, lo, hi,
		(unsigned long long)lo << PAGE_SHIFT,
		((unsigned long long)hi << PAGE_SHIFT) + PAGE_SIZE - 1, bad);
	expect("sweep mismatches", bad, 0);
	expect("sweep reaches the top of the high bank", hi + 1 >= base + size - 1024, 1);
}

static void log_page(const char *what, struct page *pg)
{
	pr_info("pgm1-user: %s pfn %07lx pa %010llx%s\n", what, page_to_pfn(pg),
		(unsigned long long)page_to_phys(pg), PageHighMem(pg) ? " (highmem)" : "");
}

/*
 * 3. Frames the page allocator hands out above 4 GiB.  Take highmem in
 * 4 MiB blocks until it runs out, keep the block at the top of the bank
 * and a pair of blocks exactly 4 GiB apart, give the rest back, and map
 * one page of each into the process through an ordinary page-backed VMA.
 */
static void highmem_alloc_test(struct mm_struct *mm, unsigned long base, unsigned long size)
{
	LIST_HEAD(blocks);
	struct page *pg, *tmp, *top = NULL, *x = NULL, *y = NULL;
	unsigned long nblocks = size >> BLOCK_ORDER, n = 0, i;
	unsigned long *have = kcalloc(BITS_TO_LONGS(nblocks), sizeof(long), GFP_KERNEL);
	struct vm_area_struct *vma;
	u8 code[64];
	u32 a, b, c;
	unsigned int faults;
	int len, k;

	if (!have) {
		failures++;
		return;
	}
	for (;;) {
		pg = alloc_pages(GFP_HIGHUSER | __GFP_NOWARN | __GFP_NORETRY, BLOCK_ORDER);
		if (!pg)
			break;
		if (!PageHighMem(pg)) {
			__free_pages(pg, BLOCK_ORDER);	/* fell back to low memory */
			break;
		}
		/* Highmem also includes RAM0 above the low-memory limit:
		 * hold those blocks too, but track only the high bank. */
		list_add(&pg->lru, &blocks);
		if (page_to_pfn(pg) >= base && page_to_pfn(pg) < base + size) {
			set_bit((page_to_pfn(pg) - base) >> BLOCK_ORDER, have);
			if (!top || page_to_pfn(pg) > page_to_pfn(top))
				top = pg;
			n++;
		}
	}
	pr_info("pgm1-user: allocated %lu highmem blocks of 4 MiB from a %lu-block bank\n",
		n, nblocks);
	sweep_blocks(&blocks, base, size);
	for (i = 0; i + BLOCKS_4G < nblocks; i++)
		if (test_bit(i, have) && test_bit(i + BLOCKS_4G, have))
			break;
	list_for_each_entry_safe(pg, tmp, &blocks, lru) {
		unsigned long idx = (page_to_pfn(pg) - base) >> BLOCK_ORDER;

		if (i + BLOCKS_4G < nblocks && idx == i)
			x = pg;
		else if (i + BLOCKS_4G < nblocks && idx == i + BLOCKS_4G)
			y = pg;
		else if (pg != top) {
			list_del(&pg->lru);
			__free_pages(pg, BLOCK_ORDER);
		}
	}
	kfree(have);
	if (!top) {
		pr_err("pgm1-user: FAIL no highmem block allocated\n");
		failures++;
		return;
	}
	expect("top block in high bank", page_to_pfn(top) + (1UL << BLOCK_ORDER) > base + size - 64 * 1024,
	       1);
	alloc_pages_test[2] = top + (1UL << BLOCK_ORDER) - 1;	/* last page of the top block */
	if (x && y) {
		alloc_pages_test[0] = x;
		alloc_pages_test[1] = y;
		expect("pair exactly 4 GiB apart",
		       page_to_phys(y) - page_to_phys(x) == (1ULL << 32), 1);
		expect("pair same low 32 bits",
		       (u32)page_to_phys(y) == (u32)page_to_phys(x), 1);
	} else {
		pr_info("pgm1-user: bank too small for a pair 4 GiB apart\n");
		alloc_pages_test[0] = top;
		alloc_pages_test[1] = top + 1;
	}
	log_page("page A", alloc_pages_test[0]);
	log_page("page B", alloc_pages_test[1]);
	log_page("page T (top of bank)", alloc_pages_test[2]);
	for (k = 0; k < 3; k++) {
		kword_set(alloc_pages_test[k], 0, 0xA11C0000u + k);
		kword_set(alloc_pages_test[k], 1, 0);
	}

	mmap_write_lock(mm);
	vma = _install_special_mapping(mm, ALLOC_VA, 3 * PAGE_SIZE,
				       VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE |
				       VM_SHARED | VM_MAYSHARE, &alloc_spec);
	mmap_write_unlock(mm);
	if (IS_ERR(vma)) {
		pr_err("pgm1-user: FAIL alloc mapping %ld\n", PTR_ERR(vma));
		failures++;
		goto out;
	}
	/* ldc V; ldnl 0 (x3): C = page 0, B = page 1, A = page 2 */
	len = 0;
	for (k = 0; k < 3; k++) {
		len += put(code + len, 0x40, ALLOC_VA + k * PAGE_SIZE);
		len += put(code + len, 0x30, 0);
	}
	len += sysc(code + len);
	i = len;
	for (k = 0; k < 3; k++) {			/* ldc w; ldc V; stnl 1 */
		len += put(code + len, 0x40, 0x5700 + k);
		len += put(code + len, 0x40, ALLOC_VA + k * PAGE_SIZE);
		len += put(code + len, 0xE0, 1);
	}
	len += sysc(code + len);
	if (copy_to_user((void __user *)CODE_VA + 0x200, code, len)) {
		failures++;
		goto out;
	}
	for (k = 0; k < 4; k++) {
		u32 pds_c;

		run(CODE_VA + 0x200, &a, &b, &faults);
		pds_c = 0;
		expect("alloc read A", a, 0xA11C0002u);
		expect("alloc read B", b, 0xA11C0001u);
		if (!k)
			expect("alloc first-touch faults", faults, 3);
		else
			expect("alloc mapped faults", faults, 0);
		(void)pds_c;
	}
	run(CODE_VA + 0x200 + i, &a, &b, &faults);
	for (k = 0; k < 3; k++)
		expect("alloc user store seen by kmap", kword(alloc_pages_test[k], 1), 0x5700 + k);
	pr_info("pgm1-user: allocator frames: user reads %08x %08x, kmap sees stores %x %x %x\n",
		b, a, kword(alloc_pages_test[0], 1), kword(alloc_pages_test[1], 1),
		kword(alloc_pages_test[2], 1));
	/* unmap, swap pages 0 and 1, map again by faulting */
	zap_vma_range(vma, ALLOC_VA, 3 * PAGE_SIZE);
	swap(alloc_pages_test[0], alloc_pages_test[1]);
	run(CODE_VA + 0x200, &a, &b, &faults);
	expect("alloc remap faults", faults, 3);
	expect("alloc remap page1 now A's", b, 0xA11C0000u);
	pr_info("pgm1-user: allocator remap faults=%u B=%08x\n", faults, b);
	swap(alloc_pages_test[0], alloc_pages_test[1]);
	zap_vma_range(vma, ALLOC_VA, 3 * PAGE_SIZE);
out:
	alloc_pages_test[0] = alloc_pages_test[1] = alloc_pages_test[2] = NULL;
	if (x)
		__free_pages(x, BLOCK_ORDER);
	if (y)
		__free_pages(y, BLOCK_ORDER);
	__free_pages(top, BLOCK_ORDER);
}
#endif

static int selftest_thread(void *unused)
{
	struct mm_struct *mm = mm_alloc();
	struct vm_area_struct *vma;
	u8 code[64];
	u32 a, b, word;
	unsigned int faults, i;
	unsigned long base, size;
	int n, composed;

	if (!mm)
		goto out;
	kthread_use_mm(mm);
	if (IS_ERR_VALUE(vm_mmap(NULL, CODE_VA, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
				 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0)) ||
	    IS_ERR_VALUE(vm_mmap(NULL, DATA_VA, PAGE_SIZE, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0)) ||
	    IS_ERR_VALUE(vm_mmap(NULL, WS_VA, PAGE_SIZE, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0))) {
		pr_err("pgm1-user: FAIL vm_mmap\n");
		failures++;
		goto unuse;
	}

	/* 1. ordinary anonymous memory */
	n = put(code, 0x40, DATA_VA);
	n += put(code + n, 0x30, 0);
	n += put(code + n, 0x80, 1);
	n += put(code + n, 0x40, DATA_VA);
	n += put(code + n, 0xE0, 0);
	n += put(code + n, 0x40, DATA_VA);
	n += put(code + n, 0x30, 0);
	n += sysc(code + n);
	if (copy_to_user((void __user *)CODE_VA, code, n)) {
		pr_err("pgm1-user: FAIL copy_to_user code\n");
		failures++;
		goto unuse;
	}
	composed = run(CODE_VA, &a, &b, &faults);
	expect("run1 trap (t.Scall)", composed, 8);
	expect("run1 faults", faults, 2);
	expect("run1 A", a, 1);
	if (get_user(word, (u32 __user *)DATA_VA))
		word = 0xdeadbeef;
	expect("run1 memory", word, 1);
	pr_info("pgm1-user: anon run1 faults=%u A=%08x mem=%08x\n", faults, a, word);
	composed = run(CODE_VA, &a, &b, &faults);
	expect("run2 faults", faults, 0);
	expect("run2 A", a, 2);
	if (get_user(word, (u32 __user *)DATA_VA))
		word = 0xdeadbeef;
	expect("run2 memory", word, 2);
	pr_info("pgm1-user: anon run2 (uninterrupted) faults=%u A=%08x mem=%08x\n",
		faults, a, word);

	/* 2. frames exactly 4 GiB apart */
	base = ((unsigned long long)pgm1_read(PGM1_HIGH_BASE_HI) << 32 |
		pgm1_read(PGM1_HIGH_BASE_LO)) >> PAGE_SHIFT;
	size = ((unsigned long long)pgm1_read(PGM1_HIGH_SIZE_HI) << 32 |
		pgm1_read(PGM1_HIGH_SIZE_LO)) >> PAGE_SHIFT;
	if (size < (1UL << 20) + 6) {
		pr_info("pgm1-user: no high bank of 4 GiB + 6 pages, skipping part 2\n");
		goto report;
	}
	high_pfn[0] = base + 5;
	high_pfn[1] = base + 5 + (1UL << 20);	/* + 4 GiB */
	pr_info("pgm1-user: frames pfn %07lx pa %010llx and pfn %07lx pa %010llx\n",
		high_pfn[0], (unsigned long long)high_pfn[0] << PAGE_SHIFT,
		high_pfn[1], (unsigned long long)high_pfn[1] << PAGE_SHIFT);
	window_set(high_pfn[0], 0, 0xAAAA0001);
	window_set(high_pfn[1], 0, 0xBBBB0002);
	expect("window A", window_word(high_pfn[0], 0), 0xAAAA0001);
	expect("window B", window_word(high_pfn[1], 0), 0xBBBB0002);

	mmap_write_lock(mm);
	vma = _install_special_mapping(mm, HIGH_VA, 2 * PAGE_SIZE,
				       VM_READ | VM_WRITE | VM_MAYREAD | VM_MAYWRITE |
				       VM_SHARED | VM_MAYSHARE | VM_PFNMAP | VM_IO,
				       &high_spec);
	mmap_write_unlock(mm);
	if (IS_ERR(vma)) {
		pr_err("pgm1-user: FAIL special mapping %ld\n", PTR_ERR(vma));
		failures++;
		goto unuse;
	}
	/* ldc H; ldnl 0; ldc H+4K; ldnl 0; syscall  -> A = B frame, B = A frame */
	n = put(code, 0x40, HIGH_VA);
	n += put(code + n, 0x30, 0);
	n += put(code + n, 0x40, HIGH_VA + PAGE_SIZE);
	n += put(code + n, 0x30, 0);
	n += sysc(code + n);
	/* ldc v; ldc H; stnl 1; ldc w; ldc H+4K; stnl 1; syscall */
	i = n;
	n += put(code + n, 0x40, 0x5A5A0011);
	n += put(code + n, 0x40, HIGH_VA);
	n += put(code + n, 0xE0, 1);
	n += put(code + n, 0x40, 0x6B6B0022);
	n += put(code + n, 0x40, HIGH_VA + PAGE_SIZE);
	n += put(code + n, 0xE0, 1);
	n += sysc(code + n);
	if (copy_to_user((void __user *)CODE_VA + 0x100, code, n)) {
		failures++;
		goto unuse;
	}
	high_faults = 0;
	run(CODE_VA + 0x100, &a, &b, &faults);
	expect("high faults", faults, 2);
	expect("high VMA faults", high_faults, 2);
	expect("high user B frame", a, 0xBBBB0002);
	expect("high user A frame", b, 0xAAAA0001);
	pr_info("pgm1-user: high read faults=%u A=%08x B=%08x\n", faults, a, b);
	run(CODE_VA + 0x100 + i, &a, &b, &faults);
	expect("high store faults", faults, 0);
	expect("window A word1", window_word(high_pfn[0], 1), 0x5A5A0011);
	expect("window B word1", window_word(high_pfn[1], 1), 0x6B6B0022);
	for (i = 0; i < 4; i++) {
		run(CODE_VA + 0x100, &a, &b, &faults);
		expect("alternate faults", faults, 0);
		expect("alternate B", a, 0xBBBB0002);
		expect("alternate A", b, 0xAAAA0001);
		expect("alternate window A", window_word(high_pfn[0], 0), 0xAAAA0001);
		expect("alternate window B", window_word(high_pfn[1], 0), 0xBBBB0002);
	}
	/* unmap, swap the frames, map again through the fault path */
	zap_special_vma_range(vma, HIGH_VA, 2 * PAGE_SIZE);
	swap(high_pfn[0], high_pfn[1]);
	high_faults = 0;
	run(CODE_VA + 0x100, &a, &b, &faults);
	expect("remap faults", faults, 2);
	expect("remap first page now B", b, 0xBBBB0002);
	expect("remap second page now A", a, 0xAAAA0001);
	pr_info("pgm1-user: remapped faults=%u A=%08x B=%08x\n", faults, a, b);

report:
#ifdef CONFIG_HIGHMEM
	if (pgm1_read(PGM1_HIGH_SIZE_LO) | pgm1_read(PGM1_HIGH_SIZE_HI))
		highmem_alloc_test(mm,
			(unsigned long)(((u64)pgm1_read(PGM1_HIGH_BASE_HI) << 32 |
					 pgm1_read(PGM1_HIGH_BASE_LO)) >> PAGE_SHIFT),
			(unsigned long)(((u64)pgm1_read(PGM1_HIGH_SIZE_HI) << 32 |
					 pgm1_read(PGM1_HIGH_SIZE_LO)) >> PAGE_SHIFT));
#endif
	pr_info("pgm1-user: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
unuse:
	kthread_unuse_mm(mm);
	mmput(mm);
out:
	complete(&selftest_done);
	return 0;
}

static int __init pgm1_user_selftest(void)
{
	if (!pgm1_present()) {
		pr_err("pgm1-user: FAIL paging not initialised\n");
		return 0;
	}
	kthread_run(selftest_thread, NULL, "pgm1-selftest");
	wait_for_completion(&selftest_done);
	return 0;
}
late_initcall(pgm1_user_selftest);

/*
 * Signals end to end, from a freshly launched userspace binary:
 * /usr/bin/sigtest (transputer/tests/user/sigtest.c) installs SIGUSR1 and
 * SA_SIGINFO SIGUSR2 handlers through uClibc, signals itself, and checks
 * the handlers, the mask and its locals after rt_sigreturn.  It reports
 * on /dev/ttyS0.  Runs once userspace is up, beside PID 1.
 */
#include <linux/delay.h>
#include <linux/umh.h>

static int signal_selftest_thread(void *unused)
{
	static char *argv[] = { "/usr/bin/sigtest", NULL };
	static char *envp[] = { "HOME=/", "PATH=/bin:/sbin:/usr/bin", NULL };
	int ret;

	static char *iso[] = { "/usr/bin/isotest", NULL };
	static char *dump[] = { "/bin/sh", "-c",
		"exec >/dev/ttyS0 2>&1; echo PROC_IOMEM_BEGIN; cat /proc/iomem; "
		"echo PROC_MEMINFO_BEGIN; cat /proc/meminfo; "
		"echo PROC_ZONEINFO_BEGIN; cat /proc/zoneinfo; echo PROC_DUMP_END", NULL };

	msleep(3000);
	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	pr_info("pgm1-signal: /usr/bin/sigtest status %d (want 0)\n", ret);
	ret = call_usermodehelper(iso[0], iso, envp, UMH_WAIT_PROC);
	pr_info("pgm1-isolation: /usr/bin/isotest status %d (want 0)\n", ret);
	ret = call_usermodehelper(dump[0], dump, envp, UMH_WAIT_PROC);
	pr_info("pgm1-procdump: status %d\n", ret);
	return 0;
}

static int __init pgm1_signal_selftest(void)
{
	if (pgm1_present())
		kthread_run(signal_selftest_thread, NULL, "pgm1-sigtest");
	return 0;
}
late_initcall(pgm1_signal_selftest);
