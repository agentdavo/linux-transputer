// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer syscall dispatch
 *
 * Both the legacy C-call gate and TP32X P-process syscall traps use
 * this dispatcher.
 *
 * This function dispatches based on the syscall number using the
 * generic syscall numbers from asm-generic/unistd.h.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/fdtable.h>
#include <linux/anon_inodes.h>
#include <linux/mm.h>
#include <linux/mman.h>
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
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/mm.h>
#include <linux/task_work.h>
#include <linux/timekeeping.h>
#include <asm/uart.h>
#include <asm/mmu_context.h>
#include <asm/tp_softprot.h>
#ifdef CONFIG_FUTEX
#include <asm/futex.h>
#endif

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

/*
 * Parent stack guard: on NOMMU, child mmap can land on the parent's
 * workspace.  setjmp only saves 8 words of the syscall frame; init's
 * caller frames sit at HIGHER addresses (stack grows down via CALL).
 * Snapshot that upper window only — never the live setjmp frame itself
 * (restoring over it re-entered the child forever).
 */
#define TP_PARENT_STACK_SNAP  0x4000UL
struct tp_private_mmap {
	struct list_head link;
	unsigned long addr;
	unsigned long len;
};

struct tp_spawn_mmap_scope {
	struct list_head mappings;
	struct tp_spawn_mmap_scope *parent;
	struct mm_struct *caller_mm;
	bool track_private;
};

static void tp_private_mmap_free(struct tp_private_mmap *map)
{
	list_del(&map->link);
	tp_forget_user_mmap((u32)map->addr, map->len);
	free_pages_exact((void *)map->addr, map->len);
	kfree(map);
}

static void tp_private_mmap_release(struct list_head *mappings)
{
	struct tp_private_mmap *map, *next;

	list_for_each_entry_safe(map, next, mappings, link)
		tp_private_mmap_free(map);
}

int init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
	INIT_LIST_HEAD(&mm->context.private_mmaps);
	return 0;
}

void destroy_context(struct mm_struct *mm)
{
	tp_private_mmap_release(&mm->context.private_mmaps);
}

static long tp_private_munmap(struct list_head *mappings, unsigned long addr,
			     unsigned long len, bool require_owned)
{
	struct tp_private_mmap *map;

	if (!PAGE_ALIGNED(addr) || !len || len > ULONG_MAX - (PAGE_SIZE - 1))
		return -EINVAL;
	len = PAGE_ALIGN(len);
	if (len > ULONG_MAX - addr)
		return -EINVAL;

	if (mappings) {
		list_for_each_entry(map, mappings, link) {
			if (addr == map->addr && len == map->len) {
				tp_private_mmap_free(map);
				return 0;
			}
			/* Only complete recorded extents can be released. */
			if (addr < map->addr + map->len && map->addr < addr + len)
				return -EINVAL;
		}
	}
	return require_owned ? -ENOENT : 0;
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
	struct tp_soft_mm protection;
	struct task_struct *owner;
	struct mm_struct *mm;
	unsigned long entry;
	unsigned long stack_top;
	unsigned long stack_base;	/* dedicated pages, 0 if using image stack */
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
struct tp_parent_stack {
	unsigned long base;
	unsigned char data[TP_PARENT_STACK_SNAP];
};

/* Only synchronous launchers need this state. It follows the Linux task
 * across CPUs and is never copied into a newly cloned thread. */
struct tp_syscall_state {
	long jump[10], nested_jump[10];
	int active, return_code;
	struct tp_parent_stack stacks[2];
	struct tp_bb_cache bb;
	volatile unsigned long hw_parent;
	unsigned long hw_stop[4];
};
#define spawn_jmpbuf (current->thread.syscall_state->jump)
#define spawn_jmpbuf_nest (current->thread.syscall_state->nested_jump)
#define spawn_active (current->thread.syscall_state->active)
#define spawn_ret_code (current->thread.syscall_state->return_code)
#define parent_stacks (current->thread.syscall_state->stacks)
#define tp_bb (current->thread.syscall_state->bb)
#define hw_spawn_parent_desc (current->thread.syscall_state->hw_parent)
#define hw_stop_workspace (current->thread.syscall_state->hw_stop)

static void tp_parent_stack_save(void)
{
	struct tp_parent_stack *snap = &parent_stacks[spawn_active];
	long *jb = spawn_active ? spawn_jmpbuf_nest : spawn_jmpbuf;
	unsigned long sp = jb[0];

	snap->base = 0;
	/* A trapped caller already has a kernel-owned supervisor workspace.
	 * Restoring a user-sized window here would overwrite live kernel data.
	 */
	if (!sp || object_is_on_stack((void *)sp))
		return;
	snap->base = ALIGN(sp + 8 * sizeof(long), 16);
	memcpy(snap->data, (void *)snap->base, TP_PARENT_STACK_SNAP);
}

static void tp_parent_stack_restore(void)
{
	struct tp_parent_stack *snap = &parent_stacks[spawn_active];

	if (snap->base)
		memcpy((void *)snap->base, snap->data, TP_PARENT_STACK_SNAP);
}


static bool tp_bb_can_reenter(void)
{
	struct tp_spawn_mmap_scope *scope;

	if (tp_bb.owner != current)
		return false;
	/* Restoring the snapshot would overwrite a suspended BusyBox applet. */
	for (scope = current->thread.spawn_mmaps; scope; scope = scope->parent) {
		if (scope->caller_mm == tp_bb.mm)
			return false;
	}
	return true;
}

static void tp_bb_release(struct task_struct *task)
{
	struct tp_syscall_state *state = task->thread.syscall_state;
	struct tp_bb_cache *cache;

	if (!state)
		return;
	cache = &state->bb;
	if (cache->owner != task)
		return;
	if (cache->img_snap)
		free_pages((unsigned long)cache->img_snap, cache->img_order);
	if (cache->stack_base)
		free_pages(cache->stack_base, TP_BB_STACK_ORDER);
	mmput(cache->mm);
	memset(cache, 0, sizeof(*cache));
}

void tp_spawn_release_task(struct task_struct *task)
{
	struct tp_spawn_mmap_scope *scope;

	while ((scope = task->thread.spawn_mmaps)) {
		task->thread.spawn_mmaps = scope->parent;
		tp_private_mmap_release(&scope->mappings);
		if (scope->caller_mm)
			mmput(scope->caller_mm);
		kfree(scope);
	}
	tp_bb_release(task);
	kfree(task->thread.syscall_state);
	task->thread.syscall_state = NULL;
}

/* NOMMU: transfer an owned user reference to current and return its old one. */
static struct mm_struct *tp_spawn_switch_mm(struct mm_struct *mm)
{
	struct mm_struct *old = current->mm;
	unsigned long flags;

	task_lock(current);
	local_irq_save(flags);
	current->mm = mm;
	current->active_mm = mm;
	local_irq_restore(flags);
	task_unlock(current);
	return old;
}

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
	if (tp_bb.img_snap && tp_bb.img_len && tp_bb.img_base) {
		memcpy((void *)tp_bb.img_base, tp_bb.img_snap, tp_bb.img_len);
	} else if (tp_bb.end_data && tp_bb.start_brk > tp_bb.end_data) {
		unsigned long n = tp_bb.start_brk - tp_bb.end_data;
		if (n < 0x200000UL)
			memset((void *)tp_bb.end_data, 0, n);
	}
	if (tp_bb.start_brk)
		tp_bb.mm->brk = (tp_bb.start_brk + 3) & ~3;
}

static int tp_bb_build_stack(const char *const *argv,
			     unsigned long *entry_out, unsigned long *wptr_out)
{
	unsigned long top;
	char *strp;
	unsigned long *sp, *argv_slot, *envp_slot;
	int argc = 0, i, nbytes = 0;
	const char *s;

	if (!argv || !argv[0])
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
 * Total size 104 bytes with 8-byte aligned 64-bit fields (st_size @ 48,
 * st_blksize @ 56, st_blocks @ 64, st_atime @ 72, st_mtime @ 80, st_ino @ 96).
 */
struct tp_uclibc_stat {
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
 * Hardware-assisted spawn using TP32 STOPP/RUNP instructions.
 * Parent truly suspends via hardware scheduling queue; child runs
 * as a real hardware-scheduled process.
 */
extern void __transputer_hw_spawn_parent(unsigned long child_wptr,
					 unsigned long child_iptr,
					 volatile unsigned long *parent_desc);
extern void __transputer_hw_resume_parent(volatile unsigned long *parent_desc,
					  unsigned long *stop_wptr)
	__attribute__((noreturn));


extern void __transputer_enter_exec(unsigned long entry, unsigned long wptr);
extern void tp_enter_userspace(unsigned long entry, unsigned long wptr);

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
/* Old opt-in clients use this flag to select the retired shared slots. */
#define TP_LEGACY_TRAP_FLAG_ADDR 0x80000064UL

typedef long (*tp_syscall_fn_t)(long, long, long, long, long, long, long);

void tp_publish_syscall_gate(void)
{
	*(volatile tp_syscall_fn_t *)TP_SYSCALL_GATE_ADDR =
		(tp_syscall_fn_t)__transputer_syscall;
	*(volatile unsigned long *)TP_LEGACY_TRAP_FLAG_ADDR = 0;
	*(volatile unsigned long *)TP_TRAP_ABI_ADDR =
		tp_hard_p_enabled() ? TP_TRAP_ABI_V1 : 0;
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
#define __NR_fcntl64        25
#define __NR_write          64
#define __NR_writev         66
#define __NR_sync           81
#define __NR_fsync          82
#define __NR_exit           93
#define __NR_exit_group     94
#define __NR_waitid         95
#define __NR_set_tid_address 96
#define __NR_tp_get_thread_pointer 2003
#define __NR_tp_set_thread_pointer 2004
#define __NR_tp_cmpxchg_user 2005
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
#define __NR_fadvise64_64   223
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
#define __NR_tp_munmap_owned 2002 /* exact private extent, or an error */
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
/* Real file-table entries avoid a global synthetic descriptor namespace. */
static ssize_t tp_null_read(struct file *file, char __user *buf, size_t count,
			    loff_t *pos)
{
	return 0;
}
static ssize_t tp_null_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *pos)
{
	return count;
}
static const struct file_operations tp_null_fops = {
	.read = tp_null_read,
	.write = tp_null_write,
	.llseek = noop_llseek,
};
static int tp_open_null(long flags)
{
	int fd = anon_inode_getfd("tp-null", &tp_null_fops, NULL, flags);

	if (fd >= 0 && fd <= 2) {
		int high = sys_fcntl(fd,
			flags & O_CLOEXEC ? F_DUPFD_CLOEXEC : F_DUPFD, 3);
		sys_close(fd);
		return high;
	}
	return fd;
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
		return tp_open_null(flags);
	return -ENOENT;
}

/*
 * getdents64 helper — classic linux_dirent64 layout with fixed d_name
 * offset 19 (TCC pads flexible arrays to 20, which breaks busybox/tman).
 * Uses iterate_dir so virtual filesystems like /proc emit PID entries.
 */
#define TP_DE_NAME   19
#define TP_DE_ALIGN(n)  (((n) + 7) & ~7U)

struct tp_getdents_buf {
	struct dir_context ctx;
	char __user *dirp;
	unsigned int count;
	unsigned int filled;
	int error;
	unsigned short prev_reclen;
};

static bool tp_filldir64(struct dir_context *ctx, const char *name,
			 int namlen, loff_t offset, u64 ino,
			 unsigned int d_type)
{
	struct tp_getdents_buf *b =
		container_of(ctx, struct tp_getdents_buf, ctx);
	unsigned int reclen;
	unsigned short rl;
	unsigned char ty;
	s64 off;
	char __user *dst;

	if (namlen < 0 || namlen > 255) {
		b->error = -EINVAL;
		return false;
	}
	reclen = TP_DE_ALIGN(TP_DE_NAME + (unsigned int)namlen + 1);
	if (b->filled + reclen > b->count) {
		/* Buffer full — stop iteration; caller keeps prior f_pos. */
		b->error = 0;
		return false;
	}

	dst = b->dirp + b->filled;
	off = (s64)offset;
	rl = (unsigned short)reclen;
	ty = (unsigned char)(d_type & 0xFF);

	if (copy_to_user(dst + 0, &ino, 8) ||
	    copy_to_user(dst + 8, &off, 8) ||
	    copy_to_user(dst + 16, &rl, 2) ||
	    copy_to_user(dst + 18, &ty, 1) ||
	    copy_to_user(dst + TP_DE_NAME, name, (unsigned long)namlen) ||
	    copy_to_user(dst + TP_DE_NAME + namlen, "", 1)) {
		b->error = -EFAULT;
		return false;
	}

	/* Patch previous entry's d_off to this offset (classic ABI). */
	if (b->prev_reclen) {
		char __user *prev = dst - b->prev_reclen;
		if (copy_to_user(prev + 8, &off, 8)) {
			b->error = -EFAULT;
			return false;
		}
	}

	b->filled += reclen;
	b->prev_reclen = rl;
	b->error = 0;
	return true;
}

static long tp_getdents64(unsigned int fd, char __user *dirp,
			  unsigned int count)
{
	struct file *file;
	struct tp_getdents_buf buf;
	long ret;

	if (count < 24)
		return -EINVAL;
	if (count > 65536)
		count = 65536;

	file = fget(fd);
	if (!file)
		return -EBADF;

	memset(&buf, 0, sizeof(buf));
	buf.ctx.actor = tp_filldir64;
	buf.ctx.pos = file->f_pos;
	buf.ctx.count = (int)count;
	buf.dirp = dirp;
	buf.count = count;

	ret = iterate_dir(file, &buf.ctx);
	file->f_pos = buf.ctx.pos;

	if (ret < 0 && buf.filled == 0) {
		fput(file);
		return ret;
	}
	if (buf.error < 0 && buf.filled == 0) {
		fput(file);
		return buf.error;
	}

	/* Final d_off on last entry = current f_pos */
	if (buf.prev_reclen) {
		char __user *last = dirp + buf.filled - buf.prev_reclen;
		s64 last_off = (s64)buf.ctx.pos;

		if (copy_to_user(last + 8, &last_off, 8)) {
			fput(file);
			return -EFAULT;
		}
	}

	{
		static atomic_t gd = ATOMIC_INIT(0);
		if (atomic_inc_return(&gd) <= 8) {
			printk("getdents64_iter: fd=%u filled=%u pos=%lld\n",
			       fd, buf.filled, (long long)buf.ctx.pos);

		}
	}
	fput(file);
	return (long)buf.filled;
}

static long tp_soft_spawn(const char *path, const char *const *argv,
			  const char *const *envp)
{
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
	struct pt_regs regs;

	if (spawn_active >= 2) {
		/* Only one nest level (init→fbwm→app). */
		return -EAGAIN;
	}

	{
		const char *p = spawn_active ? "SPAWN_NEST:" : "SPAWN:";
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
	 * Nested soft spawns use spawn_jmpbuf_nest.
	 */
	{
		long *jb = spawn_active ? spawn_jmpbuf_nest : spawn_jmpbuf;
		ret = __transputer_spawn_setjmp(jb);
	}
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
	if (tp_path_is_bin_applet(path) && tp_bb_can_reenter()) {
		unsigned long entry, wptr;
		ret = tp_bb_build_stack(argv, &entry, &wptr);
		if (ret == 0) {
			mmget(tp_bb.mm);
			mmput(tp_spawn_switch_mm(tp_bb.mm));
			regs.iptr = entry;
			regs.wptr = wptr;
			current->thread.soft_mm = tp_bb.protection;
			if (tp_bb.stack_base) {
				current->thread.soft_mm.stack_base = tp_bb.stack_base;
				current->thread.soft_mm.stack_len = TP_BB_STACK_SIZE;
			}
			spawn_active = spawn_active ? 2 : 1;
			{
				const char *pmsg = "SPAWN_GO:";
				while (*pmsg)
					uart_putc_raw(*pmsg++);
				uart_debug_hex32(regs.iptr);
				uart_putc_raw(':');
				uart_debug_hex32(regs.wptr);
				uart_putc_raw('\n');
			}
			tp_enter_userspace(regs.iptr, regs.wptr);
			spawn_active = spawn_active >= 2 ? 1 : 0;
			return -ENOEXEC;
		}
	}

	ret = kernel_execve(path, argv, envp);
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
	if (!tp_bb.mm && tp_path_is_bin_applet(path) && current->mm) {
		tp_bb.owner = current;
		tp_bb.mm = current->mm;
		mmget(tp_bb.mm);
		tp_bb.entry = regs.iptr;
		tp_bb.protection = current->thread.soft_mm;
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
	spawn_active = spawn_active ? 2 : 1;


	{
		const char *p = "SPAWN_GO:";
		while (*p)
			uart_putc_raw(*p++);
		uart_debug_hex32(regs.iptr);
		uart_putc_raw(':');
		uart_debug_hex32(regs.wptr);
		uart_putc_raw('\n');
	}

	/* Enter child as P-process (goprot) or classic L enter_exec */
	tp_enter_userspace(regs.iptr, regs.wptr);

	/* Should never reach here */
	spawn_active = spawn_active >= 2 ? 1 : 0;
	return -ENOEXEC;
}

static long tp_hw_spawn(const char *path, const char *const *argv,
			const char *const *envp)
{
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
	ret = kernel_execve(path, argv, envp);
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

/* A synchronous child execs in the caller's Linux task. Keep the parent's
 * PDS, FP/block state, RODS and access windows on this call frame until
 * the child returns (including the setjmp/longjmp and cached applet paths).
 */
static long tp_spawn_with_parent(long number, const char *path,
				 const char *const *argv, const char *const *envp)
{
	struct tp_soft_mm *mm = &current->thread.soft_mm;
	struct tp_soft_mm parent = *mm;
	struct pt_regs parent_regs = *current_pt_regs();
	unsigned long parent_tp = current->thread.user_tp;
	struct tp_spawn_mmap_scope *scope;
	struct mm_struct *child_mm;
	bool hardware;
	long ret;

	/* Synchronous exec temporarily replaces this task's image.  Other
	 * threads must not retain pointers into that image while it runs. */
	if (!thread_group_leader(current) || !thread_group_empty(current))
		return -EBUSY;

	if (!current->thread.syscall_state) {
		current->thread.syscall_state =
			kzalloc(sizeof(struct tp_syscall_state), GFP_KERNEL);
		if (!current->thread.syscall_state)
			return -ENOMEM;
	}
	hardware = number == __NR_tp_hw_spawn && !spawn_active;
#ifdef CONFIG_TRANSPUTER_SHARED_KERNEL
	/* Hardware RUNP is outside Linux scheduling; use the task-owned path. */
	hardware = false;
#endif
	if (!current->mm)
		return -EINVAL;
	/* Keep the parent image live across exec and the workspace switch. */
	scope = kmalloc(sizeof(*scope), GFP_KERNEL);
	if (!scope)
		return -ENOMEM;
	INIT_LIST_HEAD(&scope->mappings);
	scope->parent = current->thread.spawn_mmaps;
	scope->caller_mm = current->mm;
	scope->track_private = !hardware;
	mmget(scope->caller_mm);
	current->thread.spawn_mmaps = scope;
	if (hardware) {
		ret = tp_hw_spawn(path, argv, envp);
	} else {
		if (number == __NR_tp_hw_spawn) {
			const char *p = "HW_SPAWN_AS_SOFT\n";
			while (*p)
				uart_putc_raw(*p++);
		}
		ret = tp_soft_spawn(path, argv, envp);
	}

	child_mm = tp_spawn_switch_mm(scope->caller_mm);
	current->thread.spawn_mmaps = scope->parent;
	*current_pt_regs() = parent_regs;
	current->thread.user_tp = parent_tp;
	*mm = parent;
	tp_install_task_rods(current);
	tp_private_mmap_release(&scope->mappings);
	kfree(scope);
	mmput(child_mm);
	return ret;
}

void __noreturn transputer_finish_exec(struct mm_struct *old_mm, int error);
extern void __noreturn __transputer_exec_handoff(unsigned long workspace,
						struct mm_struct *old_mm,
						int error);

void __noreturn transputer_finish_exec(struct mm_struct *old_mm, int error)
{
	struct pt_regs *regs = current_pt_regs();

	tp_bb_release(current);
	mmput(old_mm);
	if (!error)
		tp_enter_userspace(regs->iptr, regs->wptr);
	do_exit(SIGSEGV);
}

static long tp_execve(const char *path, const char *const *argv,
		      const char *const *envp)
{
	struct mm_struct *old_mm = NULL;
	long ret;

	/* The legacy gate runs on the caller's bFLT stack. Keep it live
	 * while the loader replaces the image and reads the new SD file.
	 * Leave the synchronous-spawn continuation on its existing workspace.
	 */
	if (!current->thread.spawn_mmaps) {
		old_mm = current->mm;
		if (old_mm)
			mmget(old_mm);
	}
	ret = kernel_execve(path, argv, envp);
	if (old_mm) {
		/* Once the mm has changed, even failure abandons the old stack. */
		if (!ret || current->mm != old_mm)
			__transputer_exec_handoff((unsigned long)(current_pt_regs() - 1),
						  old_mm, ret);
		mmput(old_mm);
	}
	if (!ret) {
		struct pt_regs *regs = current_pt_regs();

		tp_enter_userspace(regs->iptr, regs->wptr);
		return -ENOEXEC;
	}
	return ret;
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
	if (number == __NR_execve)
		return tp_execve((const char *)a1, (const char *const *)a2,
				 (const char *const *)a3);

switch (number) {

	case __NR_open: {
		const char *path = (const char *)a1;
		long ret = tp_open_path(path, a2);
		if (ret != -ENOENT) {
			/* synthetic device or path error other than "not special" */
			{
				static atomic_t ologs = ATOMIC_INIT(0);
				if (atomic_inc_return(&ologs) <= 16) {
					printk("open: path=%s flags=%x ret=%ld (synth)\n",
					       path ? path : "(nil)", (int)a2, ret);

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
			static atomic_t ologs = ATOMIC_INIT(0);
			if (atomic_inc_return(&ologs) <= 16) {
				printk("open: path=%s flags=%x ret=%ld\n",
				       path ? path : "(nil)", (int)a2, ret);

			}
		}
		return ret;
	}

	case 39: { /* __NR_umount2 */
		long ret = sys_umount((char __user *)a1, (int)a2);

		/* This gate bypasses exit-to-user work. Complete the deferred
		 * mount teardown and its writeback before reporting success.
		 */
		if (!ret && task_work_pending(current))
			task_work_run();
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
					static atomic_t oalogs = ATOMIC_INIT(0);
					if (atomic_inc_return(&oalogs) <= 24) {
						printk("openat: dfd=%d path=%s flags=%x ret=%ld (synth)\n",
						       (int)a1, path, (int)a3, ret);

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
			static atomic_t oalogs = ATOMIC_INIT(0);
			if (atomic_inc_return(&oalogs) <= 24) {
				printk("openat: dfd=%d path=%s flags=%x ret=%ld\n",
				       (int)a1, path ? path : "(nil)",
				       (int)a3, ret);

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

	case __NR_sync:
		ksys_sync();
		return 0;

	case __NR_fsync:
		return sys_fsync((unsigned int)a1);

	case __NR_dup:
		return sys_dup((unsigned int)a1);

	case __NR_dup3:
		return sys_dup3((unsigned int)a1, (unsigned int)a2, (int)a3);

	case __NR_dup2:
		return sys_dup2((unsigned int)a1, (unsigned int)a2);

	case __NR_write: {
		/* Try VFS first so redirected stdout/stderr and shell pipes work.
		 * Fall back to direct UART only when fd 0..2 has no VFS file. */
		unsigned int fd = (unsigned int)a1;
		long ret = sys_write(fd, (const char __user *)a2, (size_t)a3);
		if (fd <= 2 && ret == -EBADF) {
			const char *buf = (const char *)a2;
			long count = a3;
			long i;
			for (i = 0; i < count; i++)
				uart_putc_raw(buf[i]);
			return count;
		}
		return ret;
	}

	case __NR_writev: {
		unsigned long fd = (unsigned long)a1;
		long ret = sys_writev(fd, (const struct iovec __user *)a2,
				      (unsigned long)a3);
		if (fd <= 2 && ret == -EBADF) {
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
		return ret;
	}

	case __NR_exit:
	case __NR_exit_group:
		if (number == __NR_exit && !thread_group_leader(current))
			return sys_exit((int)a1);
		/*
		 * Prefer hardware-spawn resume over soft setjmp/longjmp.
		 * Nested case: init soft-spawns fbwm (spawn_active=1), then
		 * fbwm hw-spawns tman.  When tman exits, taking the soft path
		 * first longjmps back to *init* and kills the auto-demo
		 * (no TMAN_DONE / no DOOM).  Hardware parent desc means this
		 * exit belongs to an hw_spawn child.
		 */
		if (current->thread.syscall_state && hw_spawn_parent_desc != 0) {
			const char *p = "HW_EXIT:RUNP\n";
			while (*p)
				uart_putc_raw(*p++);
			__transputer_hw_resume_parent(&hw_spawn_parent_desc,
						      hw_stop_workspace + 2);
		}
		if (current->thread.syscall_state && spawn_active) {
			int code = (int)a1;
			long *jb;
			const char *p = "SPAWN_EXIT:";
			while (*p)
				uart_putc_raw(*p++);
			uart_putc_raw('0' + ((code / 100) % 10));
			uart_putc_raw('0' + ((code / 10) % 10));
			uart_putc_raw('0' + (code % 10));
			uart_putc_raw('\n');
			/*
			 * Leave TP32X P-mode + clear child soft-mm before
			 * longjmp to parent.  C-call exit does not trap.
			 */
			{
				extern void tp_soft_mm_clear(struct task_struct *t);
				tp_soft_mm_clear(current);
			}
			/* Nested soft spawn returns to nest jmpbuf first. */
			if (spawn_active >= 2) {
				jb = spawn_jmpbuf_nest;
				spawn_active = 1;
			} else {
				jb = spawn_jmpbuf;
				spawn_active = 0;
			}
			__transputer_spawn_longjmp(jb, code ? code : 256);
			/* never reached */
		}
		/* The legacy top-level exit parks without reaching release_thread. */
		tp_spawn_release_task(current);
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

	case __NR_socket:
		return __sys_socket((int)a1, (int)a2, (int)a3);

	case __NR_fcntl:
		return sys_fcntl((unsigned int)a1, (unsigned int)a2,
				 (unsigned long)a3);

	case __NR_fcntl64:
		return sys_fcntl64((unsigned int)a1, (unsigned int)a2,
				   (unsigned long)a3);

	case __NR_chdir:
		return sys_chdir((const char __user *)a1);

	case __NR_setsid:
		return 1;

	case __NR_rt_sigaction:
		return 0;
	case __NR_rt_sigprocmask:
		return sys_rt_sigprocmask((int)a1, (sigset_t __user *)a2,
					  (sigset_t __user *)a3, (size_t)a4);

	case __NR_sigtimedwait:
		return sys_rt_sigtimedwait_time32((const sigset_t __user *)a1,
				(siginfo_t __user *)a2,
				(const struct old_timespec32 __user *)a3, (size_t)a4);
	case 421: /* __NR_rt_sigtimedwait_time64 */
		return sys_rt_sigtimedwait((const sigset_t __user *)a1,
				(siginfo_t __user *)a2,
				(const struct __kernel_timespec __user *)a3, (size_t)a4);

#ifdef CONFIG_EPOLL
	case 20: /* __NR_epoll_create1 */
		return sys_epoll_create1((int)a1);
	case 21: /* __NR_epoll_ctl */
		return sys_epoll_ctl((int)a1, (int)a2, (int)a3,
				     (struct epoll_event __user *)a4);
	case 22: { /* __NR_epoll_pwait */
		long ret = sys_epoll_pwait((int)a1, (struct epoll_event __user *)a2,
			(int)a3, (int)a4, (const sigset_t __user *)a5, (size_t)a6);

		/* Protected return saves the original mask in the signal frame. */
		if (ret == -EINTR && !tp_hard_p_enabled())
			restore_saved_sigmask();
		return ret;
	}
	case 441: { /* __NR_epoll_pwait2 */
		long ret = sys_epoll_pwait2((int)a1, (struct epoll_event __user *)a2,
			(int)a3, (const struct __kernel_timespec __user *)a4,
			(const sigset_t __user *)a5, (size_t)a6);

		if (ret == -EINTR && !tp_hard_p_enabled())
			restore_saved_sigmask();
		return ret;
	}
#endif

#ifdef CONFIG_POSIX_TIMERS
	case 107: /* __NR_timer_create */
		return sys_timer_create((clockid_t)a1, (struct sigevent __user *)a2,
					(timer_t __user *)a3);
	case 108: /* __NR_timer_gettime */
		return sys_timer_gettime32((timer_t)a1,
					   (struct old_itimerspec32 __user *)a2);
	case 109: /* __NR_timer_getoverrun */
		return sys_timer_getoverrun((timer_t)a1);
	case 110: /* __NR_timer_settime */
		return sys_timer_settime32((timer_t)a1, (int)a2,
			(const struct old_itimerspec32 __user *)a3,
			(struct old_itimerspec32 __user *)a4);
	case 111: /* __NR_timer_delete */
		return sys_timer_delete((timer_t)a1);
	case 408: /* __NR_timer_gettime64 */
		return sys_timer_gettime((timer_t)a1, (struct __kernel_itimerspec __user *)a2);
	case 409: /* __NR_timer_settime64 */
		return sys_timer_settime((timer_t)a1, (int)a2,
			(const struct __kernel_itimerspec __user *)a3,
			(struct __kernel_itimerspec __user *)a4);
#endif

	case __NR_getpid:
		return sys_getpid();

	case __NR_getuid:
	case __NR_getgid:
		return 0;

	case __NR_gettid:
		return sys_gettid();
	case 129: /* __NR_kill */
		return sys_kill((pid_t)a1, (int)a2);
	case 131: /* __NR_tgkill */
		return sys_tgkill((pid_t)a1, (pid_t)a2, (int)a3);
	case 153: /* __NR_times */
		return sys_times((struct tms __user *)a1);
	case 161: /* __NR_sethostname */
		return sys_sethostname((char __user *)a1, (int)a2);
	case 179: /* __NR_sysinfo */
		return sys_sysinfo((struct sysinfo __user *)a1);
	case 122: /* __NR_sched_setaffinity */
		return sys_sched_setaffinity((pid_t)a1, (unsigned int)a2,
				(const unsigned long __user *)a3);
	case 123: /* __NR_sched_getaffinity */
		return sys_sched_getaffinity((pid_t)a1, (unsigned int)a2,
				(unsigned long __user *)a3);
	case 168: /* __NR_getcpu */
		return sys_getcpu((unsigned int __user *)a1,
				(unsigned int __user *)a2, NULL);
	case 124: /* __NR_sched_yield */
		return sys_sched_yield();
	case __NR_set_tid_address:
		return sys_set_tid_address((int __user *)a1);

#ifdef CONFIG_FUTEX
	case 98: /* __NR_futex, 32-bit timespec */
		return sys_futex_time32((u32 __user *)a1, (int)a2, (u32)a3,
					(const struct old_timespec32 __user *)a4,
					(u32 __user *)a5, (u32)a6);
	case __NR_tp_cmpxchg_user: {
		struct tp_soft_mm *mm = tp_task_soft_mm(current);
		u32 old;
		int error;

		if ((a1 & 3) || !(mm->flags & TP_SOFT_FL_PRIV) ||
		    !tp_soft_ok(mm, a1, sizeof(old), 1) ||
		    !tp_private_ram(mm, tp_la_to_phys(mm, a1), sizeof(old)))
			return -EFAULT;
		pagefault_disable();
		error = futex_atomic_cmpxchg_inatomic(&old, (u32 __user *)a1, a2, a3);
		pagefault_enable();
		return error ? error : old == (u32)a2;
	}
#endif
	case __NR_tp_get_thread_pointer:
		return current->thread.user_tp;
	case __NR_tp_set_thread_pointer:
		current->thread.user_tp = a1;
		return 0;

	case __NR_clock_gettime:
		return sys_clock_gettime32((clockid_t)a1, (struct old_timespec32 __user *)a2);
	case 114: /* __NR_clock_getres */
		return sys_clock_getres_time32((clockid_t)a1, (struct old_timespec32 __user *)a2);
	case 403: /* __NR_clock_gettime64 */
		return sys_clock_gettime((clockid_t)a1, (struct __kernel_timespec __user *)a2);
	case 406: /* __NR_clock_getres_time64 */
		return sys_clock_getres((clockid_t)a1, (struct __kernel_timespec __user *)a2);

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

	case __NR_uname:
		return sys_newuname((struct new_utsname __user *)a1);

	case __NR_mmap2: {
		/* Anonymous pages from buddy allocator (not fixed 0x8FE00000 arena). */
		unsigned long len = ((unsigned long)a2 + 4095UL) & ~4095UL;
		unsigned long addr;
		struct tp_spawn_mmap_scope *scope = current->thread.spawn_mmaps;
		struct mm_struct *mm = current->mm;
		struct tp_private_mmap *map = NULL;

		/* Cap matches ARCH_FORCE_MAX_ORDER=12 (16 MiB buddy blocks);
		 * DOOM's zone arena is a single 6 MiB allocation. */
		if (len == 0 || len > (16UL << 20))
			return -EINVAL;
		if ((mm || (scope && scope->track_private)) &&
		    (a4 & (MAP_TYPE | MAP_ANONYMOUS)) ==
			     (MAP_PRIVATE | MAP_ANONYMOUS)) {
			map = kmalloc(sizeof(*map), GFP_KERNEL);
			if (!map)
				return -ENOMEM;
		}
		/*
		 * Exactly len: a power-of-two block would turn the 6 MiB
		 * arena into an 8 MiB request.  No warning either, since
		 * the caller can ask again for less.
		 */
		addr = (unsigned long)alloc_pages_exact(len,
				GFP_KERNEL | __GFP_NOWARN);
		if (!addr) {
			kfree(map);
			return -ENOMEM;
		}
		memset((void *)addr, 0, len);
		tp_note_user_mmap((u32)addr, len);
		if (map) {
			map->addr = addr;
			map->len = len;
			if (scope && scope->track_private) {
				list_add(&map->link, &scope->mappings);
			} else {
				mmap_write_lock(mm);
				list_add(&map->link, &mm->context.private_mmaps);
				mmap_write_unlock(mm);
			}
		}
		return (long)addr;
	}

	case __NR_munmap:
	case __NR_tp_munmap_owned: {
		struct tp_spawn_mmap_scope *scope = current->thread.spawn_mmaps;
		struct mm_struct *mm = current->mm;
		bool require_owned = number == __NR_tp_munmap_owned;
		long ret;

		if (scope && scope->track_private)
			return tp_private_munmap(&scope->mappings, a1, a2,
						 require_owned);
		if (!mm)
			return tp_private_munmap(NULL, a1, a2, require_owned);
		mmap_write_lock(mm);
		ret = tp_private_munmap(&mm->context.private_mmaps, a1, a2,
					require_owned);
		mmap_write_unlock(mm);
		return ret;
	}

	case __NR_fadvise64_64: {
		CLASS(fd, f)((int)a1);
		/* The 32-bit userspace frame carries each loff_t low word first. */
		loff_t offset = ((u64)(u32)a3 << 32) | (u32)a2;
		loff_t len = ((u64)(u32)a5 << 32) | (u32)a4;

		if (fd_empty(f))
			return -EBADF;
		return vfs_fadvise(fd_file(f), offset, len, (int)a6);
	}

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

	case __NR_tp_spawn:
	case __NR_tp_hw_spawn:
		return tp_spawn_with_parent(number, (const char *)a1,
			(const char *const *)a2, (const char *const *)a3);

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
		return sys_getppid();

	case __NR_geteuid:
		return 0;  /* root */

	case __NR_getegid:
		return 0;  /* root */

	case 34: /* __NR_mkdirat */
		return sys_mkdirat((int)a1, (const char __user *)a2, (umode_t)a3);

	case 37: /* __NR_linkat */
		return sys_linkat((int)a1, (const char __user *)a2,
				  (int)a3, (const char __user *)a4, (int)a5);

	case 43: /* __NR_statfs64 */
		return sys_statfs64((const char __user *)a1, (size_t)a2,
				    (struct statfs64 __user *)a3);

	case 44: /* __NR_fstatfs64 */
		return sys_fstatfs64((unsigned int)a1, (size_t)a2,
				     (struct statfs64 __user *)a3);

	case 46: /* __NR_ftruncate64 */
		return sys_ftruncate64((unsigned int)a1,
				       ((loff_t)(u32)a3 << 32) | (u32)a2);

	case 53: /* __NR_fchmodat */
		return sys_fchmodat((int)a1, (const char __user *)a2, (umode_t)a3);

	case 54: /* __NR_fchownat */
		return sys_fchownat((int)a1, (const char __user *)a2,
				    (uid_t)a3, (gid_t)a4, (int)a5);

	case 88: /* __NR_utimensat (time32) */
		return sys_utimensat_time32((int)a1, (const char __user *)a2,
					    (struct old_timespec32 __user *)a3,
					    (int)a4);

	case 412: /* __NR_utimensat_time64 */
		return sys_utimensat((int)a1, (const char __user *)a2,
				     (struct __kernel_timespec __user *)a3,
				     (int)a4);


	case 61: /* __NR_getdents64 — iterate_dir + classic fixed layout */
		return tp_getdents64((unsigned int)a1, (char __user *)a2,
				     (unsigned int)a3);


case 35: /* __NR_unlinkat */
		return sys_unlinkat((int)a1, (const char __user *)a2,
				    (int)a3);

	case 36: /* __NR_symlinkat */
		return sys_symlinkat((const char __user *)a1, (int)a2,
				     (const char __user *)a3);

	case 38: /* __NR_renameat */
		return sys_renameat((int)a1, (const char __user *)a2,
				    (int)a3, (const char __user *)a4);

case 276: /* __NR_renameat2 */
		return sys_renameat2((int)a1, (const char __user *)a2,
				     (int)a3, (const char __user *)a4,
				     (unsigned int)a5);

	case __NR_umask:
		return sys_umask((int)a1);


	case 73: /* __NR_ppoll (time32) */
		return sys_ppoll_time32((struct pollfd __user *)a1,
					(unsigned int)a2,
					(struct old_timespec32 __user *)a3,
					(const sigset_t __user *)a4,
					(size_t)a5);

	case 414: /* __NR_ppoll_time64 */
		return sys_ppoll((struct pollfd __user *)a1,
				 (unsigned int)a2,
				 (struct __kernel_timespec __user *)a3,
				 (const sigset_t __user *)a4,
				 (size_t)a5);

	case 78: /* __NR_readlinkat */
		return sys_readlinkat((int)a1, (const char __user *)a2,
				      (char __user *)a3, (int)a4);

	case 79: { /* fstatat64 uClibc LFS layout (busybox lstat path) */
		struct kstat ks;
		int err = vfs_fstatat((int)a1, (const char __user *)a2,
				      &ks, (int)a4);
		if (err)
			return err;
		return tp_cp_uclibc_stat(&ks, (void __user *)a3);
	}


case 167: /* prctl */
	return 0;

default: {
	static atomic_t unsup_count = ATOMIC_INIT(0);
	if (atomic_inc_return(&unsup_count) <= 10) {
		printk("syscall: unhandled nr=%ld\n", number);
	}
	return -ENOSYS;
}
}
}
