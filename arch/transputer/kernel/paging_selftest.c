// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/uart.h>

#define TEST_VA 0x23456234UL
#define TEST_WORD 0x5a97c31bU

static pte_t test_leaf[PTRS_PER_PTE] __aligned(PAGE_SIZE);
static unsigned int test_frame[PAGE_SIZE / sizeof(unsigned int)] __aligned(PAGE_SIZE);
static unsigned int faults;
extern unsigned int transputer_paged_read(unsigned long, unsigned long, void (*)(void));
extern void transputer_page_vector(void);
extern char transputer_paged_read_pc[];

void transputer_boot_page_fault(unsigned long va, unsigned long root, unsigned long pc)
{
	pte_t *slot = &test_leaf[pte_index(va)];

	if (++faults != 1 || va != TEST_VA || root != __pa(swapper_pg_dir) ||
	    pc != (unsigned long)transputer_paged_read_pc || !pte_none(*slot))
		panic("unexpected paging test fault va=%lx root=%lx pc=%lx", va, root, pc);
	uart_puts("\npage fault read va=");
	uart_puthex32(va);
	uart_puts(" restart=");
	uart_puthex32(pc);
	set_pte(slot, pfn_pte(virt_to_pfn(test_frame), PAGE_READONLY));
	flush_tlb_all();
	uart_puts(" mapped\n");
}

void __init transputer_paging_selftest(void)
{
	unsigned int value, uninterrupted;
	pgd_t *dir = &swapper_pg_dir[TEST_VA >> PGDIR_SHIFT];

	if (pgd_val(*dir))
		panic("paging test directory in use");
	test_frame[(TEST_VA & ~PAGE_MASK) / sizeof(unsigned int)] = TEST_WORD;
	*dir = __pgd(__pa(test_leaf) | _PAGE_PRESENT);
	value = transputer_paged_read(TEST_VA, __pa(swapper_pg_dir), transputer_page_vector);
	if (value != TEST_WORD || faults != 1)
		panic("paging retry returned %x after %u faults", value, faults);
	uninterrupted = transputer_paged_read(TEST_VA, __pa(swapper_pg_dir),
					      transputer_page_vector);
	if (uninterrupted != value || faults != 1)
		panic("mapped read returned %x after %u faults", uninterrupted, faults);
	*dir = __pgd(0);
	flush_tlb_all();
	uart_puts("paging retry PASS data=5a97c31b faults=1 reference=match\n");
}
