/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_ELF_H
#define _ASM_TRANSPUTER_ELF_H

/*
 * ELF definitions for Transputer architecture
 */

#include <linux/types.h>

/* Unofficial ELF machine number shared with the Transputer TCC target. */
#ifndef EM_TRANSPUTER
#define EM_TRANSPUTER	805
#endif

/* ELF register definitions for Transputer */
typedef unsigned long elf_greg_t;

#define ELF_NGREG 16
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* No FPU register set for basic transputer */
typedef struct { } elf_fpregset_t;

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_TRANSPUTER

/* Transputer does not support shared libraries yet */
#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk. */
#define ELF_ET_DYN_BASE		(TASK_SIZE / 3 * 2)

/* When the program starts, a1 contains a pointer to a function to be 
   registered with atexit, as per the SVR4 ABI.  A value of 0 means we 
   have no such handler.  */
#define ELF_PLAT_INIT(regs, load_addr) \
	do { \
		(regs)->sp = 0x80000000; /* Default stack pointer */ \
	} while (0)

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports. */
#define ELF_HWCAP	(0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo. */
#ifndef ELF_PLATFORM
#define ELF_PLATFORM  (NULL)
#endif

struct task_struct;
extern int dump_task_regs(struct task_struct *, elf_gregset_t *);
#define ELF_CORE_COPY_TASK_REGS dump_task_regs

#define SET_PERSONALITY(ex) \
	set_personality(PER_LINUX | (current->personality & (~PER_MASK)))

#endif /* _ASM_TRANSPUTER_ELF_H */
