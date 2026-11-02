/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_TYPES_H
#define _ASM_TRANSPUTER_TYPES_H

#include <asm-generic/types.h>

/*
 * Transputer is 32-bit only, no 64-bit atomic support
 * Don't define CONFIG_64BIT
 */
#undef CONFIG_64BIT

#endif /* _ASM_TRANSPUTER_TYPES_H */
