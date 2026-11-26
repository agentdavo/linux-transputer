// SPDX-License-Identifier: GPL-2.0
/*
 * User processes under PGM1 paging (emulator PAGING.md).
 *
 * Each user task runs as a P-process on its mm's page tables.  Its state
 * lives in current->thread.soft_mm.pds/extra (copied with thread_struct on
 * fork); the supervisor loop enters it with goprot, resolves page faults
 * through pgm1_run_p(), and serves the version-1 trap ABI the existing
 * TinyCC userspace uses: A = number, B = pointer to six argument words,
 * result back in A.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/signal.h>
#include <linux/resume_user_mode.h>
#include <asm/sigcontext.h>
#include <linux/syscalls.h>
#include <linux/binfmts.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <linux/fs.h>
#include <linux/uidgid.h>
#include <linux/cred.h>
#include <linux/init_syscalls.h>
#include <linux/major.h>
#include <linux/kdev_t.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/refcount.h>
#ifdef CONFIG_FUTEX
#include <asm/futex.h>
#endif
#include <asm/pgm1.h>
#include <asm/simplefb.h>
#include <asm/tp_softprot.h>
#include <asm/unistd.h>

extern void tp_asm_init_fp(u32 *fp);
extern void transputer_poll_tick(void);
extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);

#define PS_CNTL		0
#define PS_EPTR		1
#define PS_SWPTR	4
#define PS_SIPTR	5
#define PS_SAREG	6
#define PS_SBREG	7
#define PS_SCREG	8

#define T_ERROR		0x00
#define T_TIME		0x06
#define T_SCALL		0x08
#define ET_PRIV		0x01
#define ET_ACCESS	0x04

/* ---- generic syscall table ------------------------------------------- */

typedef long (*pgm1_sys_t)(long, long, long, long, long, long);

asmlinkage long sys_ni_syscall(void);

static long pgm1_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
		       unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
}
#define sys_mmap2 pgm1_mmap2
/* Arch entry points this port serves in pgm1_syscall() or not at all */
#define sys_rt_sigreturn sys_ni_syscall
#define sys_clone sys_ni_syscall

/*
 * The kernel's asm/unistd.h includes asm-generic/unistd.h without the
 * uapi header's __ARCH_WANT_* switches, so a table built from it lacked
 * the numbers userspace was compiled with (wait4 260 returned -ENOSYS).
 */
#ifndef __ARCH_WANT_STAT64
#define __ARCH_WANT_STAT64
#endif
#ifndef __ARCH_WANT_SET_GET_RLIMIT
#define __ARCH_WANT_SET_GET_RLIMIT
#endif
#ifndef __ARCH_WANT_TIME32_SYSCALLS
#define __ARCH_WANT_TIME32_SYSCALLS
#endif
#ifndef __ARCH_WANT_RENAMEAT
#define __ARCH_WANT_RENAMEAT
#endif

/* fstatat64/fstat64 return uClibc's layout: served in pgm1_syscall() */
#define sys_fstatat64 sys_ni_syscall
#define sys_fstat64 sys_ni_syscall

#undef __SYSCALL
#define __SYSCALL(nr, call) [nr] = (void *)(call),
static void *pgm1_sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = (void *)sys_ni_syscall,
#include <asm-generic/unistd.h>
};
#undef sys_fstatat64
#undef sys_fstat64
#undef sys_mmap2
#undef sys_rt_sigreturn
#undef sys_clone

/* ---- compatibility pages ---------------------------------------------- */

/*
 * The TinyCC userspace reads its syscall ABI tag at 0x8000006C and its
 * start-up code writes progress markers straight to the UART data word at
 * 0x10000000.  Every exec maps a read-only ABI page and the console UART
 * page there, so those binaries run unchanged.
 */
#define PGM1_ABI_VA	0x80000000UL
#define PGM1_UART_VA	0x10000000UL
#define PGM1_KBD_VA	0x11000000UL
#define PGM1_MOUSE_VA	0x12000000UL
#define PGM1_FB_FLIP	(TP_SHARED_VGA_LO + 0x14)
#define TP_TRAP_ABI_V1	0x54504601U

static struct page *pgm1_abi_page;
static struct page *pgm1_abi_pages[2];

static vm_fault_t pgm1_uart_fault(const struct vm_special_mapping *sm,
				  struct vm_area_struct *vma, struct vm_fault *vmf)
{
	return vmf_insert_pfn(vma, vmf->address & PAGE_MASK, PGM1_UART_VA >> PAGE_SHIFT);
}

static vm_fault_t pgm1_display_fault(const struct vm_special_mapping *sm,
				    struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long addr = vmf->address & PAGE_MASK;

	if (addr < TRANSPUTER_DISP_RESERVE_BASE || addr >= TRANSPUTER_DISP_RESERVE_END)
		return VM_FAULT_SIGBUS;
	return vmf_insert_pfn(vma, addr, addr >> PAGE_SHIFT);
}

static vm_fault_t pgm1_input_fault(const struct vm_special_mapping *sm,
				  struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long addr = vmf->address & PAGE_MASK;

	if (addr != PGM1_KBD_VA && addr != PGM1_MOUSE_VA)
		return VM_FAULT_SIGBUS;
	return vmf_insert_pfn(vma, addr, addr >> PAGE_SHIFT);
}

static const struct vm_special_mapping pgm1_abi_spec = {
	.name = "[tp-abi]",
	.pages = pgm1_abi_pages,
};

static const struct vm_special_mapping pgm1_uart_spec = {
	.name = "[tp-uart]",
	.fault = pgm1_uart_fault,
};

static const struct vm_special_mapping pgm1_display_spec = {
	.name = "[tp-display]",
	.fault = pgm1_display_fault,
};

static const struct vm_special_mapping pgm1_input_spec = {
	.name = "[tp-input]",
	.fault = pgm1_input_fault,
};

/*
 * The TputeOS initramfs has an empty /dev, so console_on_rootfs()
 * found no /dev/console and PID 1 started with fds 0-2 closed; its own
 * late open then returned 0 and it never dup'd 1 and 2.  Make the node
 * the kernel's default initramfs always has, after the archive is
 * unpacked (rootfs_initcall) and before init starts.
 */
static int __init pgm1_dev_console(void)
{
	int err = init_mknod("/dev/console", S_IFCHR | 0600,
			     new_encode_dev(MKDEV(TTYAUX_MAJOR, 1)));

	if (err && err != -EEXIST)
		pr_warn("pgm1: /dev/console: %d\n", err);
	return 0;
}
late_initcall(pgm1_dev_console);

static int __init pgm1_abi_init(void)
{
	u32 *w;

	pgm1_abi_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!pgm1_abi_page)
		return -ENOMEM;
	w = page_address(pgm1_abi_page);
	w[0x6C / 4] = TP_TRAP_ABI_V1;	/* 0x60 (C-call gate) and 0x64 stay 0 */
	pgm1_abi_pages[0] = pgm1_abi_page;
	return 0;
}
arch_initcall(pgm1_abi_init);

static void pgm1_exec_mappings(struct mm_struct *mm)
{
	struct vm_area_struct *vma;
	vm_flags_t ro_flags = VM_READ | VM_MAYREAD | VM_SHARED | VM_MAYSHARE |
			      VM_PFNMAP | VM_IO;
	vm_flags_t rw_flags = ro_flags | VM_WRITE | VM_MAYWRITE;

	if (mmap_write_lock_killable(mm))
		return;
	vma = _install_special_mapping(mm, PGM1_ABI_VA, PAGE_SIZE,
				       VM_READ | VM_MAYREAD, &pgm1_abi_spec);
	if (IS_ERR(vma))
		pr_warn("pgm1: no ABI page (%ld)\n", PTR_ERR(vma));
	vma = _install_special_mapping(mm, PGM1_UART_VA, PAGE_SIZE,
				       rw_flags, &pgm1_uart_spec);
	if (IS_ERR(vma))
		pr_warn("pgm1: no UART page (%ld)\n", PTR_ERR(vma));
	/* The desktop ABI addresses the framebuffer and its two reserved banks directly. */
	vma = _install_special_mapping(mm, TRANSPUTER_DISP_RESERVE_BASE,
				       TRANSPUTER_DISP_RESERVE_END - TRANSPUTER_DISP_RESERVE_BASE,
				       rw_flags, &pgm1_display_spec);
	if (IS_ERR(vma))
		pr_warn("pgm1: no display mapping (%ld)\n", PTR_ERR(vma));
	vma = _install_special_mapping(mm, PGM1_KBD_VA, PAGE_SIZE,
				       ro_flags, &pgm1_input_spec);
	if (IS_ERR(vma))
		pr_warn("pgm1: no keyboard page (%ld)\n", PTR_ERR(vma));
	vma = _install_special_mapping(mm, PGM1_MOUSE_VA, PAGE_SIZE,
				       ro_flags, &pgm1_input_spec);
	if (IS_ERR(vma))
		pr_warn("pgm1: no mouse page (%ld)\n", PTR_ERR(vma));
	mmap_write_unlock(mm);
}

/*
 * Desktop binaries present the fixed front buffer with a zero STNL to FLIP.
 * Emulate this one store so the display control page stays unmapped.
 */
static bool pgm1_fb_present(u32 *pds, u32 va, u32 acc)
{
	u32 ip = pds[PS_SIPTR], operand = 0;
	unsigned int i;

	if (va != PGM1_FB_FLIP || acc != 2 || pds[PS_SBREG] != 0)
		return false;

	for (i = 0; i < 8; i++) {
		u8 op;

		if (get_user(op, (const u8 __user *)(unsigned long)(ip + i)))
			return false;
		operand |= op & 0xf;
		switch (op >> 4) {
		case 0x2:	/* pfix */
			operand <<= 4;
			break;
		case 0x6:	/* nfix */
			operand = ~operand << 4;
			break;
		case 0xe:	/* stnl */
			if (pds[PS_SAREG] + (operand << 2) != PGM1_FB_FLIP)
				return false;
			*(volatile u32 *)PGM1_FB_FLIP = 0;
			pds[PS_SAREG] = pds[PS_SCREG];
			pds[PS_SIPTR] = ip + i + 1;
			return true;
		default:
			return false;
		}
	}
	return false;
}

/* start_thread() at the end of a successful exec: a fresh P-state. */
void pgm1_exec_setup(struct pt_regs *regs)
{
	struct tp_soft_mm *sm = &current->thread.soft_mm;
	int i;

	pgm1_exec_mappings(current->mm);
	memset(&sm->extra, 0, sizeof(sm->extra));
	tp_asm_init_fp(sm->extra.fp);
	for (i = 0; i < TP_PDS_WORDS; i++)
		sm->pds[i] = 0;
	sm->pds[PS_CNTL] = 0x80;	/* IntOvTe, as a fresh L-process */
	sm->pds[PS_SWPTR] = regs->wptr;
	sm->pds[PS_SIPTR] = regs->iptr;
}

/* ---- tp_spawn (2000): run a program, wait for it ---------------------- */

#define SPAWN_MAX_ARGS	32
#define SPAWN_MAX_BYTES	4096

struct pgm1_spawn {
	char *path;
	refcount_t refs;
	int exec_error;
	const char *argv[SPAWN_MAX_ARGS + 1];
	const char *envp[SPAWN_MAX_ARGS + 1];
	char strings[SPAWN_MAX_BYTES];
};

static void pgm1_spawn_put(struct pgm1_spawn *s)
{
	if (refcount_dec_and_test(&s->refs))
		kfree(s);
}

static int spawn_copy_vec(struct pgm1_spawn *s, const char *const __user *uvec,
			  const char **kvec, int *used)
{
	int i;

	for (i = 0; uvec && i < SPAWN_MAX_ARGS; i++) {
		const char __user *p;
		long n;

		if (get_user(p, uvec + i))
			return -EFAULT;
		if (!p)
			break;
		n = strncpy_from_user(s->strings + *used, p, SPAWN_MAX_BYTES - *used);
		if (n < 0)
			return n;
		if (*used + n >= SPAWN_MAX_BYTES)
			return -E2BIG;
		kvec[i] = s->strings + *used;
		*used += n + 1;
	}
	kvec[i] = NULL;
	return 0;
}

static int pgm1_spawn_child(void *arg)
{
	struct pgm1_spawn *s = arg;
	int ret = kernel_execve(s->path, s->argv, s->envp);

	/* Preserve the errno for callers that choose a fallback path. */
	s->exec_error = ret;
	pgm1_spawn_put(s);
	if (ret)
		do_exit(127 << 8);
	return 0;	/* ret_from_fork enters the new image */
}

static long pgm1_spawn(const long *a)
{
	struct pgm1_spawn *s = kzalloc(sizeof(*s), GFP_KERNEL);
	int used = 0, status = 0;
	long n, ret;
	pid_t pid;

	if (!s)
		return -ENOMEM;
	refcount_set(&s->refs, 1);
	n = strncpy_from_user(s->strings, (const char __user *)a[0], SPAWN_MAX_BYTES);
	ret = n < 0 ? n : n >= SPAWN_MAX_BYTES ? -ENAMETOOLONG : 0;
	if (!ret) {
		s->path = s->strings;
		used = n + 1;
		ret = spawn_copy_vec(s, (const char *const __user *)a[1], s->argv, &used);
	}
	if (!ret)
		ret = spawn_copy_vec(s, (const char *const __user *)a[2], s->envp, &used);
	if (ret)
		goto out;
	/* Shares the caller's mm only until kernel_execve() replaces it. */
	refcount_inc(&s->refs);
	pid = user_mode_thread(pgm1_spawn_child, s, CLONE_VM | CLONE_VFORK | SIGCHLD);
	if (pid < 0) {
		pgm1_spawn_put(s);
		ret = pid;
		goto out;
	}
	ret = kernel_wait(pid, &status);
	if (ret >= 0) {
		if (s->exec_error)
			ret = s->exec_error;
		else if (status & 0x7f)
			ret = 128 + (status & 0x7f);
		else
			ret = (status >> 8) & 0xff;
	}
out:
	pgm1_spawn_put(s);
	return ret;
}

/* ---- uClibc's 32-bit LFS struct stat ---------------------------------- */

/* Not the kernel's asm-generic stat64: the layout uClibc's bits/stat.h
 * gives with FILE_OFFSET64 (104 bytes, 8-byte aligned 64-bit fields). */
struct pgm1_uclibc_stat {
	u64 st_dev;          /*  0 */
	u32 __pad1;          /*  8 */
	u32 __st_ino;        /* 12 */
	u32 st_mode;         /* 16 */
	u32 st_nlink;        /* 20 */
	u32 st_uid;          /* 24 */
	u32 st_gid;          /* 28 */
	u64 st_rdev;         /* 32 */
	u32 __pad2;          /* 40 */
	u32 __pad2_align;    /* 44 */
	s64 st_size;         /* 48 */
	s32 st_blksize;      /* 56 */
	u32 __pad3_align;    /* 60 */
	s64 st_blocks;       /* 64 */
	s32 st_atime;        /* 72 */
	u32 st_atime_nsec;   /* 76 */
	s32 st_mtime;        /* 80 */
	u32 st_mtime_nsec;   /* 84 */
	s32 st_ctime;        /* 88 */
	u32 st_ctime_nsec;   /* 92 */
	u64 st_ino;          /* 96 */
};

static long pgm1_put_stat(struct kstat *ks, void __user *ubuf)
{
	struct pgm1_uclibc_stat t;

	memset(&t, 0, sizeof(t));
	t.st_dev = huge_encode_dev(ks->dev);
	t.__st_ino = (u32)ks->ino;
	t.st_mode = ks->mode;
	t.st_nlink = ks->nlink;
	t.st_uid = from_kuid_munged(current_user_ns(), ks->uid);
	t.st_gid = from_kgid_munged(current_user_ns(), ks->gid);
	t.st_rdev = huge_encode_dev(ks->rdev);
	t.st_size = ks->size;
	t.st_blksize = ks->blksize ? (s32)ks->blksize : 1024;
	t.st_blocks = ks->blocks;
	t.st_atime = ks->atime.tv_sec;
	t.st_atime_nsec = ks->atime.tv_nsec;
	t.st_mtime = ks->mtime.tv_sec;
	t.st_mtime_nsec = ks->mtime.tv_nsec;
	t.st_ctime = ks->ctime.tv_sec;
	t.st_ctime_nsec = ks->ctime.tv_nsec;
	t.st_ino = ks->ino;
	return copy_to_user(ubuf, &t, sizeof(t)) ? -EFAULT : 0;
}

static long pgm1_fstatat(int dfd, const char __user *path, void __user *buf, int flags)
{
	struct kstat ks;
	int err = vfs_fstatat(dfd, path, &ks, flags);

	return err ? err : pgm1_put_stat(&ks, buf);
}

static long pgm1_fstat(int fd, void __user *buf)
{
	struct kstat ks;
	int err = vfs_fstat(fd, &ks);

	return err ? err : pgm1_put_stat(&ks, buf);
}

/* ---- dispatch --------------------------------------------------------- */

static long pgm1_sigaction(const long *a)
{
	struct k_sigaction new_sa, old_sa;
	const struct sigaction __user *act = (const void __user *)a[1];
	struct sigaction __user *oact = (void __user *)a[2];
	int ret;

	if (a[3] != sizeof(sigset_t))
		return -EINVAL;
	if (act) {
		if (copy_from_user(&new_sa.sa, act, sizeof(*act)))
			return -EFAULT;
		if (!(new_sa.sa.sa_flags & SA_RESTORER)) {
			new_sa.sa.sa_flags |= SA_RESTORER;
			new_sa.sa.sa_restorer = (__sigrestore_t)a[4];
		}
	}
	ret = do_sigaction((int)a[0], act ? &new_sa : NULL, oact ? &old_sa : NULL);
	if (!ret && oact && copy_to_user(oact, &old_sa.sa, sizeof(*oact)))
		return -EFAULT;
	return ret;
}

static long pgm1_clone(const long *a)
{
	struct kernel_clone_args args = {
		.flags = a[0] & ~CSIGNAL,
		.exit_signal = a[0] & CSIGNAL,
		.stack = a[1],
		.parent_tid = (int __user *)a[2],
		.child_tid = (int __user *)a[3],
		.tls = a[4],
	};

	return kernel_clone(&args);
}

static long pgm1_syscall(long nr, unsigned long argp)
{
	long a[6];

	if (copy_from_user(a, (const void __user *)argp, sizeof(a)))
		return -EFAULT;
	switch (nr) {
	case 79:			/* fstatat64 */
		return pgm1_fstatat(a[0], (const char __user *)a[1], (void __user *)a[2], a[3]);
	case 80:			/* fstat64 */
		return pgm1_fstat(a[0], (void __user *)a[1]);
	case 2000:			/* tp_spawn(path, argv, envp) */
	case 2001:			/* tp_hw_spawn */
		return pgm1_spawn(a);
	case 2002:			/* tp_munmap_owned */
		return sys_munmap(a[0], a[1]);
	case 2003:			/* get thread pointer */
		return current->thread.user_tp;
	case 2004:			/* set thread pointer */
		current->thread.user_tp = a[0];
		return 0;
#ifdef CONFIG_FUTEX
	case 2005: {			/* tp_cmpxchg_user(uaddr, old, new) */
		u32 __user *uaddr = (u32 __user *)a[0];
		u32 oldval = (u32)a[1], newval = (u32)a[2], cur = 0;
		int err;

		if ((a[0] & 3) || (unsigned long)uaddr >= TASK_SIZE)
			return -EFAULT;
		for (;;) {
			pagefault_disable();
			err = futex_atomic_cmpxchg_inatomic(&cur, uaddr, oldval, newval);
			pagefault_enable();
			if (!err)
				return cur == oldval ? 1 : 0;
			if (err != -EFAULT)
				return err;
			mmap_read_lock(current->mm);
			err = fixup_user_fault(current->mm, (unsigned long)uaddr,
					       FAULT_FLAG_WRITE, NULL);
			mmap_read_unlock(current->mm);
			if (err < 0)
				return -EFAULT;
		}
	}
#endif
	case __NR_rt_sigaction:
		return pgm1_sigaction(a);
	case __NR_clone:
		return pgm1_clone(a);
	/* Legacy numbers the TinyCC syscall stub and /init still use
	 * (transputer/tests/syscall_stub.c, initramfs/init.c). */
	case 1024:			/* open */
		return sys_openat(AT_FDCWD, (const char __user *)a[0], a[1], a[2]);
	case 1027:			/* mknod */
		return sys_mknodat(AT_FDCWD, (const char __user *)a[0], a[1], a[2]);
	case 1029:			/* getcwd */
		return sys_getcwd((char __user *)a[0], a[1]);
	case 1030:			/* mkdir */
		return sys_mkdirat(AT_FDCWD, (const char __user *)a[0], a[1]);
	case 1038:			/* stat */
	case 1039:			/* lstat */
		/* These handlers use uaccess and return the uClibc stat layout. */
		return __transputer_syscall(nr, a[0], a[1], 0, 0, 0, 0);
	case 1040:			/* pipe */
		return sys_pipe2((int __user *)a[0], 0);
	case 1041:			/* dup2 */
		return a[0] == a[1] ? (sys_fcntl64(a[0], F_GETFD, 0) < 0 ? -EBADF : a[1])
				    : sys_dup3(a[0], a[1], 0);
	case 1052:			/* fcntl */
		return sys_fcntl64(a[0], a[1], a[2]);
	case 1057:			/* lseek */
		return sys_lseek(a[0], a[1], a[2]);
	case 1065:			/* getdents */
		return sys_getdents(a[0], (struct linux_dirent __user *)a[1], a[2]);
	case 1071: {			/* vfork */
		/*
		 * As fork.  The stub's vfork() is a chain of C calls; a
		 * CLONE_VM child returns through it and calls on, writing
		 * over the frames the parent is still suspended in (ash's
		 * parent then returned to 0xdd).  Linux allows vfork to be a
		 * fork, and copy-on-write keeps the stacks apart.
		 */
		long v[6] = { SIGCHLD, 0, 0, 0, 0, 0 };

		return pgm1_clone(v);
	}
	}
	if (nr >= 0 && nr < __NR_syscalls)
		return ((pgm1_sys_t)pgm1_sys_call_table[nr])(a[0], a[1], a[2],
							      a[3], a[4], a[5]);
	pr_warn_ratelimited("pgm1: %s[%d] unknown syscall %ld\n",
			    current->comm, task_pid_nr(current), nr);
	return -ENOSYS;
}

/* ---- signals ---------------------------------------------------------- */

/*
 * The frame is the NOMMU port's (tp_prot.c), which the TinyCC
 * userspace's tp_rt_sigreturn and uClibc ucontext_t already expect:
 * four linkage words the handler is entered on as if called (return
 * address = the restorer, then sig, &info, &context), the context, the
 * FP/block-move image, and siginfo.
 */
struct pgm1_signal_context {
	unsigned long flags;
	unsigned long link;
	stack_t stack;
	struct sigcontext regs;
	sigset_t mask;
};

struct pgm1_signal_frame {
	u32 linkage[4];
	struct pgm1_signal_context context;
	struct tp_p_extra extra;
	siginfo_t info;
};

/* StatusReg bits a context may carry back: the error flags and trap
 * enables goprot accepts in P-mode, and the paged block-move resume bit;
 * never privilege, timeslice or debug controls. */
#define PGM1_CNTL_USER	(0x000bffccu | PGM1_CNTL_RESUME)

static int pgm1_setup_signal(struct tp_soft_mm *sm, struct ksignal *ksig)
{
	struct pgm1_signal_frame __user *frame;
	struct pgm1_signal_context context = {};
	u32 *pds = sm->pds;
	u32 wsp = pds[PS_SWPTR], addr, linkage[4];

	if (wsp < sizeof(*frame) + PAGE_SIZE)
		return -EFAULT;
	addr = (wsp - sizeof(*frame)) & ~7u;
	frame = (void __user *)(unsigned long)addr;

	context.stack.ss_flags = SS_DISABLE;
	context.regs.wptr = wsp;
	context.regs.iptr = pds[PS_SIPTR];
	context.regs.areg = pds[PS_SAREG];
	context.regs.breg = pds[PS_SBREG];
	context.regs.creg = pds[PS_SCREG];
	context.regs.status = pds[PS_CNTL];	/* resume bit 29 included */
	context.mask = *sigmask_to_save();
	linkage[0] = (u32)(unsigned long)ksig->ka.sa.sa_restorer;
	linkage[1] = ksig->sig;
	linkage[2] = (u32)(unsigned long)&frame->info;
	linkage[3] = (u32)(unsigned long)&frame->context;
	if (copy_to_user(frame->linkage, linkage, sizeof(linkage)) ||
	    copy_to_user(&frame->context, &context, sizeof(context)) ||
	    copy_to_user(&frame->extra, &sm->extra, sizeof(sm->extra)) ||
	    ((ksig->ka.sa.sa_flags & SA_SIGINFO) &&
	     copy_siginfo_to_user(&frame->info, &ksig->info)))
		return -EFAULT;

	pds[PS_SWPTR] = addr;
	pds[PS_SIPTR] = (u32)(unsigned long)ksig->ka.sa.sa_handler;
	pds[PS_SAREG] = linkage[0];
	pds[PS_SBREG] = 0;
	pds[PS_SCREG] = 0;
	/* The handler starts with no move in progress; the interrupted one
	 * gets its resume bit back from the context. */
	pds[PS_CNTL] &= ~(0x0002aa08u | PGM1_CNTL_RESUME);
	tp_asm_init_fp(sm->extra.fp);
	memset(sm->extra.block, 0, sizeof(sm->extra.block));
	return 0;
}

/* rt_sigreturn: the handler's ret popped the four linkage words, so Wptr
 * is at the context.  Returns 0 with the interrupted state restored. */
static long pgm1_sigreturn(struct tp_soft_mm *sm)
{
	struct pgm1_signal_frame __user *frame;
	struct pgm1_signal_context context;
	struct tp_p_extra extra;
	u32 *pds = sm->pds;
	u32 addr = pds[PS_SWPTR] - sizeof(frame->linkage);

	frame = (void __user *)(unsigned long)addr;
	if ((addr & 7) ||
	    copy_from_user(&context, &frame->context, sizeof(context)) ||
	    copy_from_user(&extra, &frame->extra, sizeof(extra)) ||
	    (context.regs.wptr & 3) || context.regs.wptr >= TASK_SIZE ||
	    context.regs.iptr >= TASK_SIZE || (extra.fp[0] & ~0x57u)) {
		force_sig(SIGSEGV);
		return -EFAULT;
	}
	current->restart_block.fn = do_no_restart_syscall;
	set_current_blocked(&context.mask);
	pds[PS_SWPTR] = context.regs.wptr;
	pds[PS_SIPTR] = context.regs.iptr;
	pds[PS_SAREG] = context.regs.areg;
	pds[PS_SBREG] = context.regs.breg;
	pds[PS_SCREG] = context.regs.creg;
	pds[PS_CNTL] = (pds[PS_CNTL] & ~PGM1_CNTL_USER) |
		       (context.regs.status & PGM1_CNTL_USER);
	sm->extra = extra;
	return 0;
}

/* After a trap: the syscall result and restart rules, then any signal
 * the task has to handle before it runs again (as tp_resume_user). */
static void pgm1_resume(struct tp_soft_mm *sm, bool scall, long nr, long ret)
{
	struct ksignal ksig;
	bool deliver = signal_pending(current) && get_signal(&ksig);
	u32 *pds = sm->pds;

	if (scall) {
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
			/* Execute the syscall instruction again; B still
			 * points at the caller's argument frame. */
			pds[PS_SIPTR] = pds[PS_EPTR];
			ret = ret == -ERESTART_RESTARTBLOCK ? __NR_restart_syscall : nr;
			break;
		}
		pds[PS_SAREG] = ret;
	}
	while (deliver) {
		int err = pgm1_setup_signal(sm, &ksig);

		signal_setup_done(err, &ksig, 0);
		if (!err)
			return;
		deliver = get_signal(&ksig);	/* the bad frame queued SIGSEGV */
	}
	restore_saved_sigmask();
}

void __noreturn pgm1_user_loop(void)
{
	struct tp_soft_mm *sm = &current->thread.soft_mm;
	u32 *pds = sm->pds;

	for (;;) {
		int composed = pgm1_run_p(pds, &sm->extra, NULL);
		unsigned int reason = composed & 0xff, et = (unsigned int)composed >> 8;

		transputer_poll_tick();
		if (reason == T_SCALL) {
			long nr = (long)pds[PS_SAREG];
			long ret;

			if (nr == __NR_rt_sigreturn) {
				ret = pgm1_sigreturn(sm);
				if (!ret)
					ret = (long)pds[PS_SAREG];
			} else if (nr == __NR_restart_syscall) {
				ret = sys_restart_syscall();
			} else {
				ret = pgm1_syscall(nr, pds[PS_SBREG]);
			}
			cond_resched();
			/* After an exec the PDS is the new image's; its A is 0. */
			pgm1_resume(sm, true, nr, ret);
			continue;
		} else if (reason == T_TIME) {
			/* timeslice: run others */
		} else if (reason == T_ERROR && et == PGM1_ET_PAGEFAULT) {
			u32 va = pgm1_read(PGM1_FAULT_VA);
			u32 acc = pgm1_read(PGM1_FAULT_ACC);

			if (!pgm1_fb_present(pds, va, acc)) {
				pr_info_ratelimited("pgm1: %s[%d] SIGSEGV va=%08x acc=%x pc=%08x wp=%08x\n",
					current->comm, task_pid_nr(current), va, acc,
					pds[PS_EPTR], pds[PS_SWPTR]);
				force_sig_fault(SIGSEGV, SEGV_MAPERR,
						(void __user *)(unsigned long)va);
			}
		} else if (reason == T_ERROR && et == ET_PRIV) {
			force_sig_fault(SIGILL, ILL_PRVOPC, (void __user *)(unsigned long)pds[PS_EPTR]);
		} else {
			pr_info_ratelimited("pgm1: %s[%d] trap %02x/%02x at %08x\n",
					    current->comm, task_pid_nr(current), reason, et,
					    pds[PS_EPTR]);
			force_sig_fault(SIGSEGV, SEGV_ACCERR, (void __user *)(unsigned long)pds[PS_EPTR]);
		}
		cond_resched();
		pgm1_resume(sm, false, 0, 0);
	}
}
