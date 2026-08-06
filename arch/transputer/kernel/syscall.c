// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer syscall dispatch
 *
 * On transputer (NOMMU), there is no hardware syscall instruction.
 * Userspace calls __transputer_syscall() directly as a regular C
 * function (since there's no user/kernel boundary in NOMMU mode).
 *
 * This function dispatches based on the syscall number using the
 * generic syscall numbers from asm-generic/unistd.h.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/fdtable.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/dirent.h>
#include <linux/wait.h>
#include <asm/ptrace.h>
#include <linux/sched/task.h>
#include <linux/sched/mm.h>
#include <linux/timekeeping.h>
#include <asm/uart.h>

extern int kernel_execve(const char *filename,
			 const char *const argv[],
			 const char *const envp[]);

/* Forward declaration — defined below */
long __transputer_syscall(long number, long a1, long a2, long a3,
			  long a4, long a5, long a6);

/*
 * Spawn context for synchronous fork+exec+wait on NOMMU.
 *
 * __transputer_spawn_setjmp() saves the caller's workspace/IPtr into
 * this buffer.  When the spawned child calls exit(), the exit handler
 * calls __transputer_spawn_longjmp() to return control to the spawn
 * caller with the child's exit code.
 *
 * spawn_jmpbuf layout: 10 words
 *   [0] = caller WPtr, [1] = caller IPtr, [2..9] = workspace slots 0-7
 */
static long spawn_jmpbuf[10];
static int  spawn_active;    /* nonzero when a spawn context is live */

/*
 * Parent stack guard: on NOMMU, child mmap can land on the parent's
 * workspace.  setjmp only saves 8 words of the syscall frame; init's
 * caller frames sit at HIGHER addresses (stack grows down via CALL).
 * Snapshot that upper window only — never the live setjmp frame itself
 * (restoring over it re-entered the child forever).
 */
#define TP_PARENT_STACK_SNAP  0x4000UL
static unsigned char parent_stack_snap[TP_PARENT_STACK_SNAP];
static unsigned long parent_stack_snap_base;
static int parent_stack_snap_valid;

static void tp_parent_stack_save(void)
{
	unsigned long sp = (unsigned long)spawn_jmpbuf[0];
	if (!sp)
		return;
	/* Above the 8 saved slots of the setjmp frame */
	parent_stack_snap_base = (sp + 32UL + 15UL) & ~15UL;
	memcpy(parent_stack_snap, (void *)parent_stack_snap_base,
	       TP_PARENT_STACK_SNAP);
	parent_stack_snap_valid = 1;
}

static void tp_parent_stack_restore(void)
{
	if (!parent_stack_snap_valid || !parent_stack_snap_base)
		return;
	memcpy((void *)parent_stack_snap_base, parent_stack_snap,
	       TP_PARENT_STACK_SNAP);
}

/*
 * Track anonymous pages allocated during a tp_spawn child.
 * free_pages on exit is deferred (caused hangs); tracking kept for later.
 */
#define TP_SPAWN_MMAP_MAX 48
static struct {
	unsigned long addr;
	int order;
} spawn_mmaps[TP_SPAWN_MMAP_MAX];
static int spawn_mmap_count;

static void tp_spawn_mmap_reset(void)
{
	spawn_mmap_count = 0;
}

static void tp_spawn_mmap_track(unsigned long addr, int order)
{
	if (spawn_mmap_count >= TP_SPAWN_MMAP_MAX)
		return;
	spawn_mmaps[spawn_mmap_count].addr = addr;
	spawn_mmaps[spawn_mmap_count].order = order;
	spawn_mmap_count++;
}

/*
 * Multi-applet cache: first successful tp_spawn of /bin/* loads busybox once.
 * Later /bin/* spawns rebuild argv on the same image and re-enter — full
 * second do_execve of LFS busybox WILDJUMPs after PBINPRM.
 */
/*
 * Dedicated reenter stack size.  Child syscalls run ON THE CHILD WORKSPACE
 * in this NOMMU port; a large busybox stack inside the bFLT mapping also
 * sits near free RAM that malloc/get_free_pages may hand out.  Isolating
 * the reenter stack stops ls from smashing the parent's setjmp frames
 * (post-SPAWN_DONE bad-Icode crash).
 */
#define TP_BB_STACK_ORDER  6			/* 256 KiB */
#define TP_BB_STACK_SIZE   (PAGE_SIZE << TP_BB_STACK_ORDER)

struct tp_bb_cache {
	int valid;
	unsigned long entry;
	unsigned long end_brk;
	unsigned long stack_top;
	unsigned long stack_base;	/* dedicated pages, 0 if using image stack */
	unsigned long start_data;
	unsigned long end_data;
	unsigned long start_brk;
	/*
	 * Busybox bFLT has data_len=0 (init data lives in the text blob).
	 * Snapshot [img_base, start_brk) post-reloc so reenter is pristine.
	 */
	unsigned long img_base;
	void *img_snap;
	unsigned long img_len;
	unsigned int img_order;
};
static struct tp_bb_cache tp_bb;

/*
 * True for multi-call busybox links under /bin (or busybox itself).
 * Real binaries /bin/tpsh and anything under /usr must use full do_execve.
 * Avoid vfs_stat here — calling it right after do_execve hung the boot.
 */
static int tp_path_is_bin_applet(const char *path)
{
	const char *base;
	const char *s;

	if (!path)
		return 0;
	if (!(path[0] == '/' && path[1] == 'b' && path[2] == 'i' &&
	      path[3] == 'n' && path[4] == '/' && path[5] != '\0'))
		return 0;
	base = path + 5;
	for (s = base; *s; s++) {
		if (*s == '/')
			base = s + 1;
	}
	/* Standalone binaries staged next to busybox applets */
	if (base[0] == 't' && base[1] == 'p' && base[2] == 's' &&
	    base[3] == 'h' && base[4] == '\0')
		return 0;
	if (base[0] == 'f' && base[1] == 'b' && base[2] == 'w' &&
	    base[3] == 'm' && base[4] == '\0')
		return 0;
	return 1;
}

static int tp_bb_strlen(const char *s)
{
	int n = 0;
	if (!s)
		return 0;
	while (s[n])
		n++;
	return n;
}

/*
 * Restore code+init-data+bss to the post-load image and reset brk.
 * Without this, later applets (ls) WILDJUMP after true/uname/echo dirty
 * globals that live in the text blob (bFLT data_len=0).
 */
static void tp_bb_restore_image(void)
{
	if (!tp_bb.valid)
		return;
	if (tp_bb.img_snap && tp_bb.img_len && tp_bb.img_base) {
		memcpy((void *)tp_bb.img_base, tp_bb.img_snap, tp_bb.img_len);
	} else if (tp_bb.end_data && tp_bb.start_brk > tp_bb.end_data) {
		unsigned long n = tp_bb.start_brk - tp_bb.end_data;
		if (n < 0x200000UL)
			memset((void *)tp_bb.end_data, 0, n);
	}
	if (current->mm && tp_bb.start_brk) {
		current->mm->start_brk = tp_bb.start_brk;
		current->mm->brk = (tp_bb.start_brk + 3) & ~3;
		current->mm->end_data = tp_bb.end_data;
	}
}

static int tp_bb_build_stack(const char *const *argv,
			     unsigned long *entry_out, unsigned long *wptr_out)
{
	unsigned long top;
	char *strp;
	unsigned long *sp, *argv_slot, *envp_slot;
	int argc = 0, i, nbytes = 0;
	const char *s;

	if (!tp_bb.valid || !argv || !argv[0])
		return -ENOENT;

	for (i = 0; argv[i]; i++) {
		argc++;
		nbytes += tp_bb_strlen(argv[i]) + 1;
		if (argc > 16)
			return -E2BIG;
	}
	if (argc < 1)
		return -EINVAL;

	/* Match Linux create_flat_tables(): argc, argvp, envp, then tables. */
	top = tp_bb.stack_top & ~3UL;
	nbytes = (nbytes + 15) & ~15;
	strp = (char *)(top - (unsigned long)nbytes);

	{
		/* argc + argv + envp + argv[] + NULL + env NULL + AT_NULL*2 */
		int slots = 3 + argc + 1 + 1 + 2;
		sp = (unsigned long *)strp - slots;
		sp = (unsigned long *)(((unsigned long)sp) & ~15UL);
	}

	argv_slot = sp + 3;
	envp_slot = argv_slot + argc + 1;

	sp[0] = (unsigned long)argc;
	sp[1] = (unsigned long)argv_slot;
	sp[2] = (unsigned long)envp_slot;

	for (i = 0; i < argc; i++) {
		s = argv[i];
		argv_slot[i] = (unsigned long)strp;
		while (*s)
			*strp++ = *s++;
		*strp++ = 0;
	}
	argv_slot[argc] = 0;

	envp_slot[0] = 0;
	envp_slot[1] = 0;
	envp_slot[2] = 0;

	tp_bb_restore_image();

	*entry_out = tp_bb.entry;
	*wptr_out = (unsigned long)sp;

	{
		const char *pmsg = "BB_REENTER:";
		while (*pmsg)
			uart_putc_raw(*pmsg++);
		uart_debug_hex32(*entry_out);
		uart_putc_raw(':');
		uart_debug_hex32(*wptr_out);
		uart_putc_raw('\n');
	}
	return 0;
}



/*
 * uClibc 32-bit LFS struct stat layout (bits/stat.h + FILE_OFFSET64).
 * Not the same as kernel asm-generic stat64.
 */
struct tp_uclibc_stat {
	u64 st_dev;
	u32 __pad1;
	u32 __st_ino;
	u32 st_mode;
	u32 st_nlink;
	u32 st_uid;
	u32 st_gid;
	u64 st_rdev;
	u32 __pad2;
	s64 st_size;
	s32 st_blksize;
	s64 st_blocks;
	s32 st_atime;
	u32 st_atime_nsec;
	s32 st_mtime;
	u32 st_mtime_nsec;
	s32 st_ctime;
	u32 st_ctime_nsec;
	u64 st_ino;
} __packed;

static int tp_cp_uclibc_stat(struct kstat *ks, void __user *ubuf)
{
	struct tp_uclibc_stat tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = huge_encode_dev(ks->dev);
	tmp.__st_ino = (u32)ks->ino;
	tmp.st_mode = ks->mode;
	tmp.st_nlink = ks->nlink;
	tmp.st_uid = from_kuid_munged(current_user_ns(), ks->uid);
	tmp.st_gid = from_kgid_munged(current_user_ns(), ks->gid);
	tmp.st_rdev = huge_encode_dev(ks->rdev);
	tmp.st_size = ks->size;
	tmp.st_blksize = ks->blksize ? (s32)ks->blksize : 1024;
	tmp.st_blocks = ks->blocks;
	tmp.st_atime = ks->atime.tv_sec;
	tmp.st_atime_nsec = ks->atime.tv_nsec;
	tmp.st_mtime = ks->mtime.tv_sec;
	tmp.st_mtime_nsec = ks->mtime.tv_nsec;
	tmp.st_ctime = ks->ctime.tv_sec;
	tmp.st_ctime_nsec = ks->ctime.tv_nsec;
	tmp.st_ino = ks->ino;

	if (copy_to_user(ubuf, &tmp, sizeof(tmp)))
		return -EFAULT;
	return 0;
}


extern int __transputer_spawn_setjmp(long *env);
extern void __transputer_spawn_longjmp(long *env, int exit_code)
	__attribute__((noreturn));

/*
 * Hardware-assisted spawn using T800 STOPP/RUNP instructions.
 * Parent truly suspends via hardware scheduling queue; child runs
 * as a real hardware-scheduled process.
 */
extern void __transputer_hw_spawn_parent(unsigned long child_wptr,
					 unsigned long child_iptr,
					 volatile unsigned long *parent_desc);
extern void __transputer_hw_resume_parent(volatile unsigned long *parent_desc);

/* Storage for the parent's process descriptor — child reads this to RUNP parent */
static volatile unsigned long hw_spawn_parent_desc;
static volatile unsigned long *hw_spawn_parent_desc_ptr = &hw_spawn_parent_desc;

extern void __transputer_enter_exec(unsigned long entry, unsigned long wptr);

/*
 * Syscall gateway for NOMMU userspace.
 *
 * The bFLT binary has its own __transputer_syscall() stub, but for
 * operations that need kernel VFS (mount, stat, execve, mknod, etc.)
 * the stub reads this function pointer and calls through it.
 *
 * The kernel publishes this gate during early init so that any
 * userspace binary can find it at a fixed address.
 */
#define TP_SYSCALL_GATE_ADDR 0x80000060UL

typedef long (*tp_syscall_fn_t)(long, long, long, long, long, long, long);

void tp_publish_syscall_gate(void)
{
	*(volatile tp_syscall_fn_t *)TP_SYSCALL_GATE_ADDR =
		(tp_syscall_fn_t)__transputer_syscall;
}

long __transputer_getpid0(void)
{
	return 1;
}

long __transputer_ioctl0(long fd, long cmd, long arg)
{
	return sys_ioctl((int)fd, (unsigned int)cmd, (unsigned long)arg);
}

/* Generic syscall numbers (from asm-generic/unistd.h) */
#define __NR_dup            23
#define __NR_dup3           24
#define __NR_pipe2          59
#define __NR_ioctl          29
#define __NR_fstat          80
#define __NR_mount          40
#define __NR_chdir          49
#define __NR_faccessat      48
#define __NR_openat         56
#define __NR_close          57
#define __NR_read           63
#define __NR_write          64
#define __NR_writev         66
#define __NR_exit           93
#define __NR_exit_group     94
#define __NR_waitid         95
#define __NR_set_tid_address 96
#define __NR_clock_gettime  113
#define __NR_rt_sigaction   134
#define __NR_rt_sigprocmask 135
#define __NR_sigtimedwait   137
#define __NR_setsid         157
#define __NR_uname          160
#define __NR_getpid         172
#define __NR_getuid         174
#define __NR_getgid         176
#define __NR_gettid         178
#define __NR_brk            214
#define __NR_munmap         215
#define __NR_execve         221
#define __NR_mmap2          222
#define __NR_wait4          260
#define __NR_getcwd          17
#define __NR_getcwd_legacy 1029
#define __NR_open          1024
#define __NR_mkdir         1030
#define __NR_mknod         1027
#define __NR_stat          1038
#define __NR_lstat         1039
#define __NR_dup2          1041
#define __NR_pipe          1040
#define __NR_fcntl         1052
#define __NR_lseek         1057
#define __NR_vfork         1071
#define __NR_tp_spawn      2000  /* transputer: synchronous spawn(path, argv, envp) */
#define __NR_tp_hw_spawn   2001  /* transputer: hardware STOPP/RUNP spawn */
#define __NR_getppid       173
#define __NR_getegid       177
#define __NR_geteuid       175
#define __NR_umask          166

#define ENOTTY              25
#define ENOENT              2
#define EINVAL              22
#define EAGAIN              11
#define ECHILD              10
#define EMFILE              24
#define ENOEXEC             8

#define TP_AT_FDCWD         (-100)
#define TP_MAX_FDS          16
#define TP_FD_CONSOLE       1
#define TP_FD_NULL          2
#define TP_FD_ROOT          3
#define TP_FD_SOCKET        4

struct tp_fd_state {
	int kind;
	long flags;
};

static struct tp_fd_state tp_fd_table[TP_MAX_FDS] = {
	[0] = { TP_FD_CONSOLE, 0 },
	[1] = { TP_FD_CONSOLE, 0 },
	[2] = { TP_FD_CONSOLE, 0 },
};

static int tp_fd_valid(int fd)
{
	return fd >= 0 && fd < TP_MAX_FDS && tp_fd_table[fd].kind != 0;
}

static int tp_fd_alloc(int kind, long flags)
{
	int fd;

	for (fd = 3; fd < TP_MAX_FDS; fd++) {
		if (tp_fd_table[fd].kind == 0) {
			tp_fd_table[fd].kind = kind;
			tp_fd_table[fd].flags = flags;
			return fd;
		}
	}

	return -EMFILE;
}

static int tp_open_path(const char *path, long flags)
{
	if (!path)
		return -EFAULT;
	/*
	 * Synthetic console devices only.  Do NOT intercept "/" or other
	 * real paths — that broke openat("/") for getdents/ls.
	 * VFS mknod of tty nodes is not enough on this port (ENOENT);
	 * write(fd<=2)/read(0) already use MMIO UART.
	 */
	if (!strcmp(path, "/dev/console") ||
	    !strcmp(path, "/dev/ttyS0") ||
	    !strcmp(path, "/dev/ttyS1") ||
	    !strcmp(path, "/dev/tty")) {
		return 0; /* stdin as console handle */
	}
	if (!strcmp(path, "/dev/null"))
		return tp_fd_alloc(TP_FD_NULL, flags);
	return -ENOENT;
}

/*
 * __transputer_syscall - main syscall entry point
 *
 * Called directly from userspace via C function call.
 * Parameters are passed via the normal transputer calling convention
 * (first 3 in AReg/BReg/CReg, rest on workspace).
 */
long __transputer_syscall(long number, long a1, long a2, long a3,
			  long a4, long a5, long a6)
{
	if (number == __NR_execve) {
		long ret = kernel_execve((const char *)a1,
					 (const char *const *)a2,
					 (const char *const *)a3);

		if (ret == 0) {
			struct pt_regs *regs = current_pt_regs();

			__transputer_enter_exec(regs->iptr, regs->wptr);
			return -ENOEXEC;
		}
		return ret;
	}

switch (number) {

	case __NR_open: {
		const char *path = (const char *)a1;
		long ret = tp_open_path(path, a2);
		if (ret != -ENOENT) {
			/* synthetic device or path error other than "not special" */
			{
				static int ologs;
				if (ologs < 16) {
					printk("open: path=%s flags=%x ret=%ld (synth)\n",
					       path ? path : "(nil)", (int)a2, ret);
					ologs++;
				}
			}
			return ret;
		}
		ret = sys_open((const char __user *)a1, (int)a2, (int)a3);
		/* Keep VFS files off UART fds 0-2 — see the openat case. */
		if (ret >= 0 && ret <= 2) {
			long nfd = sys_fcntl((unsigned int)ret, F_DUPFD, 3);
			if (nfd >= 3) {
				sys_close((unsigned int)ret);
				ret = nfd;
			}
		}
		{
			static int ologs;
			if (ologs < 16) {
				printk("open: path=%s flags=%x ret=%ld\n",
				       path ? path : "(nil)", (int)a2, ret);
				ologs++;
			}
		}
		return ret;
	}

	case __NR_mount:
		return sys_mount((char __user *)a1,
				 (char __user *)a2,
				 (char __user *)a3,
				 (unsigned long)a4,
				 (void __user *)a5);

	case __NR_mkdir:
		return sys_mkdir((const char __user *)a1, (int)a2);

	case __NR_openat: {
		const char *path = (const char *)a2;
		long ret;
		/* Absolute special devices even via openat */
		if (path && path[0] == '/') {
			ret = tp_open_path(path, a3);
			if (ret != -ENOENT) {
				{
					static int oalogs;
					if (oalogs < 24) {
						printk("openat: dfd=%d path=%s flags=%x ret=%ld (synth)\n",
						       (int)a1, path, (int)a3, ret);
						oalogs++;
					}
				}
				return ret;
			}
		}
		ret = sys_openat((int)a1, (const char __user *)a2,
				     (int)a3, (int)a4);
		/*
		 * fds 0-2 are hardcoded to the UART in the hybrid I/O
		 * read/write paths, but a spawned child's fd table starts
		 * empty, so a real file can land on fd 0 (DOOM's WAD did —
		 * its directory reads then returned UART garbage).  Dup any
		 * low fd up past 2 so VFS files never collide with UART.
		 */
		if (ret >= 0 && ret <= 2) {
			long nfd = sys_fcntl((unsigned int)ret, F_DUPFD, 3);
			if (nfd >= 3) {
				sys_close((unsigned int)ret);
				ret = nfd;
			}
		}
		{
			static int oalogs;
			if (oalogs < 24) {
				printk("openat: dfd=%d path=%s flags=%x ret=%ld\n",
				       (int)a1, path ? path : "(nil)",
				       (int)a3, ret);
				oalogs++;
			}
		}
		return ret;
	}

	case __NR_faccessat:
		/* DOOM's IdentifyVersion probes WAD paths via access(). */
		return sys_faccessat((int)a1, (const char __user *)a2,
				     (int)a3);

	case 62: /* __NR_llseek — 32-bit asm-generic 5-arg _llseek;
		  * uClibc lseek/lseek64 both route through it (DOOM's
		  * W_AddFile seeks the WAD directory). */
		return sys_llseek((unsigned int)a1, (unsigned long)a2,
				  (unsigned long)a3, (loff_t __user *)a4,
				  (unsigned int)a5);

	case __NR_close:
		return sys_close((unsigned int)a1);

	case __NR_dup:
		return sys_dup((unsigned int)a1);

	case __NR_dup3:
		return sys_dup3((unsigned int)a1, (unsigned int)a2, (int)a3);

	case __NR_dup2:
		return sys_dup2((unsigned int)a1, (unsigned int)a2);

	case __NR_write: {
		/* For fd 1/2, write directly to UART for reliable console output.
		 * For other fds, use kernel VFS for real file I/O. */
		unsigned int fd = (unsigned int)a1;
		if (fd <= 2) {
			const char *buf = (const char *)a2;
			long count = a3;
			long i;
			for (i = 0; i < count; i++)
				uart_putc_raw(buf[i]);
			return count;
		}
		if (fd < TP_MAX_FDS && tp_fd_table[fd].kind == TP_FD_NULL)
			return a3; /* /dev/null */
		if (fd < TP_MAX_FDS && tp_fd_table[fd].kind == TP_FD_CONSOLE) {
			const char *buf = (const char *)a2;
			long count = a3;
			long i;
			for (i = 0; i < count; i++)
				uart_putc_raw(buf[i]);
			return count;
		}
		return sys_write(fd, (const char __user *)a2, (size_t)a3);
	}

	case __NR_writev: {
		unsigned long fd = (unsigned long)a1;
		if (fd <= 2) {
			struct { const void *base; long len; } *iov = (void *)a2;
			int iovcnt = (int)a3;
			long total = 0;
			int i;
			for (i = 0; i < iovcnt; i++) {
				const char *p = iov[i].base;
				long j;
				for (j = 0; j < iov[i].len; j++)
					uart_putc_raw(p[j]);
				total += iov[i].len;
			}
			return total;
		}
		return sys_writev(fd, (const struct iovec __user *)a2,
				  (unsigned long)a3);
	}

	case __NR_exit:
	case __NR_exit_group:
		if (spawn_active) {
			int code = (int)a1;
			const char *p = "SPAWN_EXIT:";
			while (*p)
				uart_putc_raw(*p++);
			uart_putc_raw('0' + ((code / 100) % 10));
			uart_putc_raw('0' + ((code / 10) % 10));
			uart_putc_raw('0' + (code % 10));
			uart_putc_raw('\n');
			spawn_active = 0;
			/* longjmp back to the tp_spawn caller */
			__transputer_spawn_longjmp(spawn_jmpbuf, code ? code : 256);
			/* never reached */
		}
		/* Check if this is a hw_spawn child — RUNP the parent */
		if (hw_spawn_parent_desc != 0) {
			const char *p = "HW_EXIT:RUNP\n";
			while (*p)
				uart_putc_raw(*p++);
			__transputer_hw_resume_parent(&hw_spawn_parent_desc);
			hw_spawn_parent_desc = 0;
			/* Child STOPPs itself after RUNPing parent */
			__asm__ __volatile__(".byte 0x21, 0xF5"); /* stopp */
			while (1) ;
		}
		__asm__ __volatile__(".byte 0x21, 0xF5");
		while (1)
			;
		return 0;

	case __NR_brk:
		/* Per-process heap — fixed 0x8FC00000 collided with init/high loads */
		return sys_brk((unsigned long)a1);

	case __NR_read: {
		/*
		 * Prefer VFS for all fds.  Previously fd0 was hardwired to UART,
		 * which breaks getdents/read when openat("/") is the first open
		 * and lands on fd 0 (no console yet, or console open failed).
		 * UART console input is only used if VFS returns -EBADF.
		 */
		unsigned int fd = (unsigned int)a1;
		long ret = sys_read(fd, (char __user *)a2, (size_t)a3);
		if (fd == 0 && ret == -EBADF) {
			char *buf = (char *)a2;
			long count = a3;
			long i;
			for (i = 0; i < count; i++) {
				volatile unsigned int *uart_st =
					(volatile unsigned int *)0x10000004;
				volatile unsigned int *uart_dt =
					(volatile unsigned int *)0x10000000;
				long polls = 0;
				while (!((*uart_st) & 0x02)) {
					if (++polls > 50000000L)
						return i > 0 ? i : 0;
				}
				buf[i] = (char)(*uart_dt);
				if (buf[i] == '\n' || buf[i] == '\r')
					return i + 1;
			}
			return count;
		}
		return ret;
	}

	case __NR_lseek:
		return sys_lseek((unsigned int)a1, (off_t)a2, (unsigned int)a3);

	case __NR_ioctl: {
		int fd = (int)a1;
		unsigned int cmd = (unsigned int)a2;
		unsigned long arg = (unsigned long)a3;
		long ret;

		ret = sys_ioctl(fd, cmd, arg);
		return ret;
	}

	case __NR_socket: {
		int family = (int)a1;
		int type = (int)a2;
		int protocol = (int)a3;
		int flags;
		int fd;
		struct socket *sock;

		flags = type & ~SOCK_TYPE_MASK;
		type &= SOCK_TYPE_MASK;
		if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
			flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;
		fd = sock_create(family, type, protocol, &sock);
		if (fd < 0)
			return fd;
		fd = sock_map_fd(sock, flags & (O_CLOEXEC | O_NONBLOCK));
		if (fd < 0) {
			sock_release(sock);
			return fd;
		}
		if (fd >= TP_MAX_FDS) {
			sys_close(fd);
			return -EMFILE;
		}
		tp_fd_table[fd].kind = TP_FD_SOCKET;
		tp_fd_table[fd].flags = type & ~SOCK_TYPE_MASK;
		return fd;
	}

	case __NR_fcntl:
		return sys_fcntl((unsigned int)a1, (unsigned int)a2,
				 (unsigned long)a3);

	case __NR_chdir:
		return sys_chdir((const char __user *)a1);

	case __NR_setsid:
		return 1;

	case __NR_rt_sigaction:
	case __NR_rt_sigprocmask:
		return 0;

	case __NR_sigtimedwait:
		return -EAGAIN;

	case __NR_getpid:
		return 1;

	case __NR_getuid:
	case __NR_getgid:
		return 0;

	case __NR_gettid:
	case __NR_set_tid_address:
		return 1;

	case __NR_clock_gettime: {
		long *tp = (long *)a2;

		if (tp) {
			tp[0] = 0;
			tp[1] = 0;
		}
		return 0;
	}

	case 169: { /* __NR_gettimeofday — DOOM's I_GetTime derives game
		     * tics from it, so it must actually ADVANCE (the
		     * LDTIMER-backed clocksource feeds timekeeping). */
		struct timespec64 ts;
		long *tv = (long *)a1;

		if (tv) {
			ktime_get_real_ts64(&ts);
			tv[0] = (long)ts.tv_sec;
			tv[1] = (long)(ts.tv_nsec / 1000);
		}
		return 0;
	}

	case 101: /* __NR_nanosleep — emulated time is expensive; DOOM's
		   * I_WaitVBL usleep just returns immediately. */
		return 0;

	case __NR_uname: {
		/* Fill struct utsname: 6 fixed-size fields of _UTSNAME_LENGTH (65)
		 * bytes each — sysname, nodename, release, version, machine,
		 * domainname.  NOMMU: the user pointer is directly writable.
		 * Values are copied byte-by-byte (each string literal lives in
		 * kernel .rodata; a helper loop avoids any pointer-array reloc). */
		char *u = (char *)a1;
		int f;
		const char *fields[6];

		if (!u)
			return -EINVAL;

		fields[0] = "Linux";
		fields[1] = "transputer";
		fields[2] = "2.6.39";
		fields[3] = "#1 TransputerOS";
		fields[4] = "transputer";
		fields[5] = "(none)";

		memset(u, 0, 6 * 65);
		for (f = 0; f < 6; f++) {
			const char *s = fields[f];
			char *d = u + f * 65;
			while (*s)
				*d++ = *s++;
		}
		return 0;
	}

	case __NR_mmap2: {
		/* Anonymous pages from buddy allocator (not fixed 0x8FE00000 arena). */
		unsigned long len = ((unsigned long)a2 + 4095UL) & ~4095UL;
		unsigned long addr;
		int order;

		/* Cap matches ARCH_FORCE_MAX_ORDER=12 (16 MiB buddy blocks);
		 * DOOM's zone arena is a single 6 MiB allocation. */
		if (len == 0 || len > (16UL << 20))
			return -EINVAL;
		order = get_order(len);
		addr = __get_free_pages(GFP_KERNEL, order);
		if (!addr)
			return -ENOMEM;
		memset((void *)addr, 0, len);
		if (spawn_active)
			tp_spawn_mmap_track(addr, order);
		return (long)addr;
	}

	case __NR_munmap:
		return 0;

	case __NR_vfork:
		{
			/* NOMMU vfork shares the address space and blocks the
			 * parent until the child execs or exits. */
			struct kernel_clone_args args = {
				.flags = CLONE_VFORK | CLONE_VM,
				.exit_signal = SIGCHLD,
			};

			return kernel_clone(&args);
		}

	case __NR_tp_spawn: {
		/*
		 * Synchronous spawn: load + exec child, return when it exits.
		 *
		 * a1 = path, a2 = argv, a3 = envp
		 *
		 * Uses setjmp/longjmp to save the caller's workspace.
		 * When the child calls exit(), the exit handler longjmps
		 * back here with the exit code.
		 *
		 * Returns: child's exit code (>0), or negative errno.
		 */
		int ret;
		const char *path = (const char *)a1;
		const char *const *argv = (const char *const *)a2;
		const char *const *envp = (const char *const *)a3;
		struct pt_regs regs;

		if (spawn_active) {
			/* Nested spawn not supported */
			return -EAGAIN;
		}

		{
			const char *p = "SPAWN:";
			while (*p)
				uart_putc_raw(*p++);
			p = path ? path : "(null)";
			while (*p)
				uart_putc_raw(*p++);
			uart_putc_raw('\n');
		}

		/*
		 * setjmp returns 0 on first call, and the exit code
		 * when longjmp fires from the child's exit().
		 */
		ret = __transputer_spawn_setjmp(spawn_jmpbuf);
		if (ret != 0) {
			/*
			 * We're back from the child's exit.
			 * ret is the raw value from longjmp:
			 *   256 means child called exit(0)
			 *   otherwise it's the actual exit code
			 *
			 * Capture code into a static BEFORE parent stack
			 * restore — restore can clobber this frame's locals.
			 */
			static int spawn_ret_code;
			const char *p = "SPAWN_DONE:";
			spawn_ret_code = (ret == 256) ? 0 : ret;
			while (*p)
				uart_putc_raw(*p++);
			uart_putc_raw('0' + ((ret / 100) % 10));
			uart_putc_raw('0' + ((ret / 10) % 10));
			uart_putc_raw('0' + (ret % 10));
			uart_putc_raw('\n');
			tp_parent_stack_restore();
			{
				const char *p2 = "SPAWN_RET:";
				while (*p2)
					uart_putc_raw(*p2++);
				uart_debug_hex32((unsigned long)spawn_ret_code);
				uart_putc_raw('\n');
			}
			return spawn_ret_code;
		}

		/* First call — load and enter the child */
		tp_parent_stack_save();
		memset(&regs, 0, sizeof(regs));

		/* Re-enter cached busybox for multi-applet (skip second do_execve). */
		if (tp_bb.valid && tp_path_is_bin_applet(path)) {
			unsigned long entry, wptr;
			ret = tp_bb_build_stack(argv, &entry, &wptr);
			if (ret == 0) {
				regs.iptr = entry;
				regs.wptr = wptr;
				spawn_active = 1;
				tp_spawn_mmap_reset();
				{
					const char *pmsg = "SPAWN_GO:";
					while (*pmsg)
						uart_putc_raw(*pmsg++);
					uart_debug_hex32(regs.iptr);
					uart_putc_raw(':');
					uart_debug_hex32(regs.wptr);
					uart_putc_raw('\n');
				}
				__transputer_enter_exec(regs.iptr, regs.wptr);
				spawn_active = 0;
				return -ENOEXEC;
			}
		}

		/*
		 * Pin the caller's mm before exec: kernel_execve swaps
		 * current->mm and mmput()s the old one, freeing the CALLER'S
		 * bFLT image pages while the spawn setjmp still points into
		 * them.  Once child allocations reuse those pages (deep hush
		 * init was the first to apply enough pressure) the parent's
		 * text is zeroed and the post-SPAWN_RET return wild-jumps.
		 * The extra reference keeps the image resident for the
		 * lifetime of the system; the same pin also keeps the tp_bb
		 * cached busybox image valid across later execs.
		 */
		if (current->mm)
			mmget(current->mm);
		ret = kernel_execve(path,
				    (const char *const *)argv,
				    (const char *const *)envp);
		if (ret < 0) {
			const char *pmsg = "SPAWN_FAIL:";
			while (*pmsg)
				uart_putc_raw(*pmsg++);
			uart_debug_hex32((unsigned long)ret);
			uart_putc_raw('\n');
			return ret;
		}
		regs = *current_pt_regs();

		/* Cache busybox image for later /bin/* reenter */
		if (tp_path_is_bin_applet(path) && current->mm) {
			tp_bb.valid = 1;
			tp_bb.entry = regs.iptr;
			tp_bb.end_brk = current->mm->context.end_brk;
			tp_bb.start_data = current->mm->start_data;
			tp_bb.end_data = current->mm->end_data;
			tp_bb.start_brk = current->mm->start_brk;
			/*
			 * Prefer a dedicated 256K stack away from the bFLT
			 * image so child heap/stack growth and syscall depth
			 * cannot smash the parent's setjmp workspace.
			 */
			if (!tp_bb.stack_base) {
				unsigned long pgs = __get_free_pages(
					GFP_KERNEL, TP_BB_STACK_ORDER);
				if (pgs) {
					memset((void *)pgs, 0, TP_BB_STACK_SIZE);
					tp_bb.stack_base = pgs;
					tp_bb.stack_top = pgs + TP_BB_STACK_SIZE;
				}
			}
			if (!tp_bb.stack_base) {
				/* Fallback: image-local stack */
				tp_bb.stack_top =
					(regs.wptr & ~0xfffUL) + 0x2000UL;
				if (tp_bb.stack_top < regs.wptr + 256)
					tp_bb.stack_top = regs.wptr + 0x1000UL;
			}
			/*
			 * Snapshot code+init-data+bss (data_len may be 0 — init
			 * data lives in the text blob for this busybox bFLT).
			 */
			if (!tp_bb.img_snap && current->mm->start_code &&
			    tp_bb.start_brk > current->mm->start_code) {
				unsigned long base = current->mm->start_code;
				unsigned long n = tp_bb.start_brk - base;
				unsigned int order = 0;
				if (n > 0 && n < 0x200000UL) {
					while (order < 12 &&
					       ((PAGE_SIZE << order) < n))
						order++;
					tp_bb.img_snap = (void *)
						__get_free_pages(GFP_KERNEL, order);
					if (tp_bb.img_snap) {
						memcpy(tp_bb.img_snap,
						       (void *)base, n);
						tp_bb.img_base = base;
						tp_bb.img_len = n;
						tp_bb.img_order = order;
					}
				}
			}
			{
				const char *pmsg = "BB_CACHE:";
				while (*pmsg)
					uart_putc_raw(*pmsg++);
				uart_debug_hex32(tp_bb.entry);
				uart_putc_raw(':');
				uart_debug_hex32(tp_bb.stack_top);
				uart_putc_raw(':');
				uart_debug_hex32(tp_bb.stack_base);
				uart_putc_raw(':');
				uart_debug_hex32(tp_bb.img_len);
				uart_putc_raw('\n');
			}
		}

		/* Mark spawn as active so exit() knows to longjmp */
		spawn_active = 1;
		tp_spawn_mmap_reset();


		{
			const char *p = "SPAWN_GO:";
			while (*p)
				uart_putc_raw(*p++);
			uart_debug_hex32(regs.iptr);
			uart_putc_raw(':');
			uart_debug_hex32(regs.wptr);
			uart_putc_raw('\n');
		}

		/* Enter child — this "returns" via longjmp when child exits */
		__transputer_enter_exec(regs.iptr, regs.wptr);

		/* Should never reach here */
		spawn_active = 0;
		return -ENOEXEC;
	}

	case __NR_tp_hw_spawn: {
		/*
		 * Hardware-assisted spawn using T800 STOPP/RUNP.
		 *
		 * a1 = path, a2 = argv, a3 = envp
		 *
		 * The parent truly suspends via STOPP — the hardware removes
		 * it from the scheduling queue. The child runs as a real
		 * hardware-scheduled process. When the child calls exit(),
		 * the exit handler RUNPs the parent back to life.
		 *
		 * This is TRUE concurrency: the child runs on the hardware
		 * process queue alongside any other transputer processes.
		 */
		const char *path = (const char *)a1;
		const char *const *argv = (const char *const *)a2;
		const char *const *envp = (const char *const *)a3;
		struct pt_regs regs;
		int ret;

		{
			const char *p = "HW_SPAWN:";
			while (*p) uart_putc_raw(*p++);
			p = path ? path : "(null)";
			while (*p) uart_putc_raw(*p++);
			uart_putc_raw('\n');
		}

		/* Load the child binary */
		memset(&regs, 0, sizeof(regs));
		/* Pin the caller's mm — see the tp_spawn comment above. */
		if (current->mm)
			mmget(current->mm);
		ret = kernel_execve(path,
				    (const char *const *)argv,
				    (const char *const *)envp);
		if (ret < 0)
			return ret;
		regs = *current_pt_regs();

		hw_spawn_parent_desc = 0;

		{
			const char *p = "HW_SPAWN_GO:";
			while (*p) uart_putc_raw(*p++);
			uart_debug_hex32(regs.iptr);
			uart_putc_raw(':');
			uart_debug_hex32(regs.wptr);
			uart_putc_raw('\n');
		}

		/*
		 * This call:
		 * 1. Stores child IPtr at child_wptr[-1]
		 * 2. Saves parent descriptor at hw_spawn_parent_desc
		 * 3. RUNPs the child (adds to hardware queue)
		 * 4. STOPPs the parent (suspends — removed from queue)
		 * 5. When child exits and RUNPs us, we resume here
		 */
		__transputer_hw_spawn_parent(regs.wptr, regs.iptr,
					     &hw_spawn_parent_desc);

		/* We're back! Child has exited. */
		{
			const char *p = "HW_SPAWN_DONE\n";
			while (*p) uart_putc_raw(*p++);
		}
		return 0;
	}

	case __NR_wait4:
		return sys_wait4((pid_t)a1, (int __user *)a2,
				 (int)a3, (struct rusage __user *)a4);

	case __NR_waitid:
		return sys_waitid((int)a1, (pid_t)a2,
				  (struct siginfo __user *)a3,
				  (int)a4, (struct rusage __user *)a5);

	case __NR_mknod:
		return sys_mknod((const char __user *)a1, (int)a2,
				 (unsigned)a3);

	case __NR_stat: {
		/* uClibc expects LFS layout via tp_cp_uclibc_stat */
		struct kstat ks;
		int err = vfs_stat((const char __user *)a1, &ks);
		if (err)
			return err;
		return tp_cp_uclibc_stat(&ks, (void __user *)a2);
	}

	case __NR_lstat: {
		struct kstat ks;
		int err = vfs_lstat((const char __user *)a1, &ks);
		if (err)
			return err;
		return tp_cp_uclibc_stat(&ks, (void __user *)a2);
	}

	case __NR_fstat: {
		struct kstat ks;
		int err = vfs_fstat((unsigned int)a1, &ks);
		if (err)
			return err;
		return tp_cp_uclibc_stat(&ks, (void __user *)a2);
	}

	case __NR_getcwd:
	case __NR_getcwd_legacy:
		return sys_getcwd((char __user *)a1, (unsigned long)a2);

	case __NR_pipe:
		return sys_pipe((int __user *)a1);

	case __NR_pipe2:
		return sys_pipe2((int __user *)a1, (int)a2);

	case __NR_getppid:
		return 0;  /* PID 1 has no parent */

	case __NR_geteuid:
		return 0;  /* root */

	case __NR_getegid:
		return 0;  /* root */


	case 61: { /* __NR_getdents64 — direct walk, fixed layout */
		/*
		 * filldir_t ABI is broken on this port. Walk dcache and emit
		 * classic linux_dirent64 with FIXED offsets (no compiler padding):
		 *   0:u64 ino, 8:s64 off, 16:u16 reclen, 18:u8 type, 19:name...
		 * TCC pads flexible-array structs so offsetof(d_name)==20, which
		 * makes busybox readdir see empty names and ls stat "/bin/" forever.
		 */
		unsigned int fd = (unsigned int)a1;
		char __user *dirp = (char __user *)a2;
		unsigned int count = (unsigned int)a3;
		struct file *file;
		struct dentry *dentry, *child;
		long filled = 0;
		unsigned int seen;

		/* Classic linux_dirent64 name offset */
#define TP_DE_NAME  19
#define TP_DE_ALIGN(n)  (((n) + 7) & ~7U)

		/* Never write more than the user asked — expanding a small
		 * buffer was a footgun; clamp only the upper bound. */
		if (count < 24)
			return -EINVAL;
		if (count > 65536)
			count = 65536;

		file = fget(fd);
		if (!file)
			return -EBADF;
		dentry = file->f_path.dentry;
		if (!dentry || !dentry->d_inode) {
			fput(file);
			return -ENOENT;
		}

		if (file->f_pos >= 100000) {
			fput(file);
			return 0;
		}

		if (file->f_pos == 0) {
			const char *name = ".";
			unsigned int reclen = TP_DE_ALIGN(TP_DE_NAME + 2);
			u64 ino = dentry->d_inode->i_ino;
			s64 off = 1;
			unsigned short rl = (unsigned short)reclen;
			unsigned char ty = 4; /* DT_DIR */
			if (filled + reclen > count)
				goto out_done;
			if (copy_to_user(dirp + filled + 0, &ino, 8) ||
			    copy_to_user(dirp + filled + 8, &off, 8) ||
			    copy_to_user(dirp + filled + 16, &rl, 2) ||
			    copy_to_user(dirp + filled + 18, &ty, 1) ||
			    copy_to_user(dirp + filled + TP_DE_NAME, name, 2))
				goto out_fault;
			filled += reclen;
			file->f_pos = 1;
		}
		if (file->f_pos == 1) {
			const char *name = "..";
			unsigned int reclen = TP_DE_ALIGN(TP_DE_NAME + 3);
			struct dentry *parent = dentry->d_parent ? dentry->d_parent : dentry;
			u64 ino = parent->d_inode ? parent->d_inode->i_ino : 1;
			s64 off = 2;
			unsigned short rl = (unsigned short)reclen;
			unsigned char ty = 4;
			if (filled + reclen > count)
				goto out_done;
			if (copy_to_user(dirp + filled + 0, &ino, 8) ||
			    copy_to_user(dirp + filled + 8, &off, 8) ||
			    copy_to_user(dirp + filled + 16, &rl, 2) ||
			    copy_to_user(dirp + filled + 18, &ty, 1) ||
			    copy_to_user(dirp + filled + TP_DE_NAME, name, 3))
				goto out_fault;
			filled += reclen;
			file->f_pos = 2;
		}

		seen = 0;
		spin_lock(&dentry->d_lock);
		child = d_first_child(dentry);
		while (child) {
			unsigned int namelen, reclen, skip_n;
			unsigned char dtype = 0;
			u64 ino;
			s64 off;
			unsigned short rl;
			unsigned char ty;

			if (!child->d_inode) {
				child = d_next_sibling(child);
				continue;
			}
			if (child->d_name.len == 1 && child->d_name.name[0] == '.') {
				child = d_next_sibling(child);
				continue;
			}

			skip_n = (file->f_pos >= 2) ? (unsigned int)(file->f_pos - 2) : 0;
			if (seen < skip_n) {
				seen++;
				child = d_next_sibling(child);
				continue;
			}
			seen++;

			namelen = child->d_name.len;
			reclen = TP_DE_ALIGN(TP_DE_NAME + namelen + 1);
			if (filled + reclen > count) {
				spin_unlock(&dentry->d_lock);
				goto out_done;
			}
			if (S_ISDIR(child->d_inode->i_mode))
				dtype = 4;
			else if (S_ISLNK(child->d_inode->i_mode))
				dtype = 10;
			else if (S_ISREG(child->d_inode->i_mode))
				dtype = 8;
			else if (S_ISCHR(child->d_inode->i_mode))
				dtype = 2;
			else if (S_ISBLK(child->d_inode->i_mode))
				dtype = 6;
			else
				dtype = 0;

			ino = child->d_inode->i_ino;
			off = file->f_pos + 1;
			rl = (unsigned short)reclen;
			ty = dtype;
			spin_unlock(&dentry->d_lock);

			if (copy_to_user(dirp + filled + 0, &ino, 8) ||
			    copy_to_user(dirp + filled + 8, &off, 8) ||
			    copy_to_user(dirp + filled + 16, &rl, 2) ||
			    copy_to_user(dirp + filled + 18, &ty, 1) ||
			    copy_to_user(dirp + filled + TP_DE_NAME,
					 child->d_name.name, namelen) ||
			    copy_to_user(dirp + filled + TP_DE_NAME + namelen,
					 "", 1))
				goto out_fault;

			filled += reclen;
			file->f_pos++;
			spin_lock(&dentry->d_lock);
			child = d_first_child(dentry);
			seen = 0;
		}
		spin_unlock(&dentry->d_lock);
		file->f_pos = 100000;

out_done:
		{
			static int gd;
			if (gd < 16) {
				printk("getdents64_direct: fd=%u filled=%ld pos=%lu\n",
				       fd, filled, file->f_pos);
				gd++;
			}
		}
		fput(file);
		return filled;

out_fault:
		fput(file);
		return -EFAULT;
	}


case 35: /* __NR_unlinkat */
		return sys_unlinkat((int)a1, (const char __user *)a2,
				    (int)a3);

	case 38: /* __NR_renameat */
		return sys_renameat((int)a1, (const char __user *)a2,
				    (int)a3, (const char __user *)a4);

	case __NR_umask: {
		static int current_umask = 022;
		int old = current_umask;

		current_umask = (int)a1 & 0777;
		return old;
	}


	case 79: { /* fstatat64 uClibc LFS layout (busybox lstat path) */
struct kstat ks;
int err;
{
static int flogs;
if (flogs < 24) {
const char *path = (const char *)a2;
printk("fstatat64: dfd=%d path=%s flags=%x\n",
       (int)a1, path ? path : "(nil)", (int)a4);
flogs++;
}
}
err = vfs_fstatat((int)a1, (const char __user *)a2, &ks, (int)a4);
if (err) {
static int felogs;
if (felogs < 12) {
printk("fstatat64: err=%d\n", err);
felogs++;
}
return err;
}
return tp_cp_uclibc_stat(&ks, (void __user *)a3);
}


case 167: /* prctl */
	return 0;

default: {
	static int unsup_count;
	if (unsup_count < 10) {
		printk("syscall: unhandled nr=%ld\n", number);
		unsup_count++;
	}
	return -ENOSYS;
}
}
}
