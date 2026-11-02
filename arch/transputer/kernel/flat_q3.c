// SPDX-License-Identifier: GPL-2.0
/*
 * Phase 9 multi-quarter: WPtr/stack in Q3 via T9000 region relocate.
 *
 * Use a full-quarter Q3→Q2 mirror: LA 0xC0000000+ maps to phys 0x80000000+
 * (same offset in the lower 1 GiB).  WPtr becomes stack_phys + 0x40000000.
 * This matches the T9000 1 GiB region model and avoids fragile sub-window
 * size alignment against the bFLT image.
 */
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/flat.h>
#include <asm/page.h>
#include <asm/t9000_softprot.h>

int transputer_flat_q3_publish(void)
{
	struct mm_struct *m = current->mm;
	u32 stack_phys, base, sz, la_base, delta;

	if (!m || !m->start_code || !m->start_data || !m->start_stack)
		return 0;
	if (((m->start_stack >> 30) & 3ul) == 3ul)
		return 0;

	stack_phys = (u32)m->start_stack;
	/* Stack must live in Q2 phys for the identity dual. */
	if (((stack_phys >> 30) & 3u) != 2u) {
		pr_info_once("flat_q3: skip non-Q2 stack=%08x\n", stack_phys);
		return 0;
	}

	/* Full Q2 mirror in Q3: phys 0x80000000, size 1 GiB. */
	base = 0x80000000u;
	sz = 1u << 30;
	la_base = T9K_USER_DATA_QBASE; /* 0xC0000000 */
	delta = la_base - base;        /* 0x40000000 */

	m->start_stack = (unsigned long)stack_phys + delta;
	if (m->arg_start)
		m->arg_start += delta;
	if (m->arg_end)
		m->arg_end += delta;
	if (m->env_start)
		m->env_start += delta;
	if (m->env_end)
		m->env_end += delta;

	t9k_note_split_load(base, la_base, sz);

	pr_info_once("flat_q3: WPtr→Q3 stack %08x→%08lx (full Q2 mirror)\n",
		     stack_phys, m->start_stack);
	return 0;
}
EXPORT_SYMBOL(transputer_flat_q3_publish);
