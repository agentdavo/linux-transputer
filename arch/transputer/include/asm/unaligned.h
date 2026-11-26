/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer unaligned access support
 */
#ifndef _ASM_TRANSPUTER_UNALIGNED_H
#define _ASM_TRANSPUTER_UNALIGNED_H

/*
 * Transputer doesn't support unaligned accesses
 * Use the generic implementation that handles byte access
 */
#include <linux/unaligned.h>

#endif /* _ASM_TRANSPUTER_UNALIGNED_H */
