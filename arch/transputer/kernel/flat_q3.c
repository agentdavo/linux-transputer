// SPDX-License-Identifier: GPL-2.0
/*
 * Phase 9 multi-quarter: WPtr/stack in Q3 via T9000 region relocate.
 *
 * Phys window is size-aligned and covers the stack with downward headroom.
 * Lower bound is the private image base (bFLT textpos), not start_data —
 * so workspace growth can use the process's own pages (dual-mapped with Q2)
 * without aliasing foreign phys.
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
	u32 stack_phys, map_lo, page_hi, page_bot;
	u32 base, sz, la_base, delta;

	if (!m || !m->start_code || !m->start_data || !m->start_stack)
		return 0;
	if (((m->start_stack >> 30) & 3ul) == 3ul)
		return 0;

	stack_phys = (u32)m->start_stack;
	/* Private image base: flat header just before start_code. */
	map_lo = (u32)(m->start_code - sizeof(struct flat_hdr));
	map_lo &= ~(u32)(PAGE_SIZE - 1);

	page_hi = stack_phys + (u32)PAGE_SIZE;
	page_bot = (stack_phys > map_lo + 0x10000u)
			? (stack_phys - 0x10000u) : map_lo;

	base = 0;
	sz = 0;
	for (sz = 4096u; sz && sz <= (1u << 30); sz <<= 1) {
		u32 b = page_bot & ~(sz - 1u);

		if (b < map_lo)
			continue;
		if (b + sz < page_hi)
			continue;
		if (stack_phys < b || stack_phys >= b + sz)
			continue;
		base = b;
		break;
	}
	if (!base || !sz) {
		pr_info_once("flat_q3: skip stack=%08x map_lo=%08x\n",
			     stack_phys, map_lo);
		return 0;
	}

	la_base = T9K_USER_DATA_QBASE;
	delta = la_base - base;

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

	pr_info_once("flat_q3: WPtr→Q3 stack %08x→%08lx base=%08x sz=%08x\n",
		     stack_phys, m->start_stack, base, sz);
	return 0;
}
EXPORT_SYMBOL(transputer_flat_q3_publish);
