/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TRANSPUTER_MMU_CONTEXT_H
#define __TRANSPUTER_MMU_CONTEXT_H

struct mm_struct;
struct task_struct;
int init_new_context(struct task_struct *tsk, struct mm_struct *mm);
void destroy_context(struct mm_struct *mm);
#define init_new_context init_new_context
#define destroy_context destroy_context

#include <asm-generic/nommu_context.h>

#endif /* __TRANSPUTER_MMU_CONTEXT_H */
