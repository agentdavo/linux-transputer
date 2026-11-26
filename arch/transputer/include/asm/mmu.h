#ifndef _ASM_TRANSPUTER_MMU_H
#define _ASM_TRANSPUTER_MMU_H

#ifndef __ASSEMBLER__
#include <linux/types.h>

typedef struct {
	unsigned long end_brk;
#ifdef CONFIG_BINFMT_ELF_FDPIC
	unsigned long exec_fdpic_loadmap;
	unsigned long interp_fdpic_loadmap;
#endif
	struct list_head private_mmaps;
} mm_context_t;
#endif

#endif
