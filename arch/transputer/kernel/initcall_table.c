// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer initcall table — linker-populated function pointer array
 *
 * TCC's linker ignores linker-script symbol assignments, so the standard
 * __initcall_start / __initcall_end / __early_initcall_end symbols that
 * normally come from vmlinux.lds don't exist.  Each __define_initcall()
 * creates a scattered function pointer in .data; the kernel's
 * do_initcalls() loop has nothing to iterate.
 *
 * Fix: define __initcall_start as a real 128-slot array.  The transputer
 * linker (transputer-link.c tp_populate_initcall_table) scans all
 * __initcall_* symbols, sorts them by level, and writes the function
 * pointers into this array.  Before relocation it defines every Linux 7
 * level boundary as an absolute symbol at the correct array offset.
 *
 * The boundary names below provide definitions during vmlinux.o/modpost.
 * The final Transputer link changes their symbols to SHN_ABS before normal
 * relocation, so the placeholder storage is not observed by the kernel.
 */

typedef int (*initcall_t)(void);

/*
 * The consolidated initcall table.  128 slots is enough for the ~65
 * initcalls in the current transputer kernel build, with room to grow.
 * Initialized to zero so unused slots are NULL (do_one_initcall skips
 * NULL entries on some paths, and the loop terminates at __initcall_end).
 */
initcall_t __initcall_start[128]
	__attribute__((section(".initcall.table"), used, aligned(4))) = { 0 };

/*
 * Surrogate boundary markers.  These are .long words whose ADDRESSES
 * are used by init/main.c (via extern initcall_t __early_initcall_end[]).
 * transputer-link.c resolves these names to offsets inside the table before
 * applying R_TRANSPUTER_32 and R_TRANSPUTER_LDC_ABS relocations.
 *
 * Initialized to 1 so they go in .data (not .bss) and the linker can
 * find and patch them reliably.
 */
initcall_t __early_initcall_end[1] = { (initcall_t)1 };
initcall_t __initcall0_start[1]    = { (initcall_t)1 };
initcall_t __initcall1_start[1]    = { (initcall_t)1 };
initcall_t __initcall2_start[1]    = { (initcall_t)1 };
initcall_t __initcall3_start[1]    = { (initcall_t)1 };
initcall_t __initcall4_start[1]    = { (initcall_t)1 };
initcall_t __initcall5_start[1]    = { (initcall_t)1 };
initcall_t __initcall6_start[1]    = { (initcall_t)1 };
initcall_t __initcall7_start[1]    = { (initcall_t)1 };
initcall_t __initcall_end[1]       = { (initcall_t)1 };

/*
 * Kernel command-line parameter table surrogates.
 * parse_early_param() and obsolete_checksetup() iterate from
 * __setup_start to __setup_end.  Define as empty so the loops
 * don't execute (no __setup() entries are critical for boot).
 */
struct obs_kernel_param {
	const char *str;
	int (*setup_func)(char *);
	int early;
};

struct obs_kernel_param __setup_end[1]   = { { 0, 0, 0 } };
struct obs_kernel_param __setup_start[1] = { { 0, 0, 0 } };
