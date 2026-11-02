/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_THDS_H
#define _ASM_TRANSPUTER_THDS_H

/* ISM Table 10.1 / PROCESS.md — THDS word offsets (bytes = word * 4) */
#define THDS_CNTL	0
#define THDS_IPTR	1
#define THDS_FPTR	2
#define THDS_BPTR	3
#define THDS_WORDS	12

/* Default control word: only sb.IntOvTeBit (ISM §5.2) */
#define T9000_STATUS_DEFAULT_CW	0x00000080u
#define T9000_NOT_PROCESS_P	0x80000000u
#define T9000_TH_IN_USE		(1u << 31)

#ifndef __ASSEMBLY__

void t9k_asm_selth(unsigned long thds_or_null);
unsigned long t9k_asm_ldth(void);
void t9k_ltrap_entry(void);

#ifdef CONFIG_TRANSPUTER_L_THDS_BOOT
int t9k_thds_boot_install(void);
#else
static inline int t9k_thds_boot_install(void) { return 0; }
#endif

#endif /* __ASSEMBLY__ */
#endif
