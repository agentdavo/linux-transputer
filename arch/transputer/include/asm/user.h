/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TRANSPUTER_USER_H
#define _ASM_TRANSPUTER_USER_H

/*
 * Core file format definitions for Transputer
 * This is a minimal implementation for kernel compilation
 */

/* Transputer doesn't have a traditional user struct like other architectures */
struct user {
    /* Empty for now - transputer doesn't have debug registers */
};

#endif /* _ASM_TRANSPUTER_USER_H */
