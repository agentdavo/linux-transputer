// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer interrupt handling — 2.6.39 style (no IRQ domains)
 *
 * The Transputer doesn't have a traditional interrupt controller.
 * This provides a minimal simulated interrupt system for Linux.
 *
 * Based on h8300/kernel/irq.c
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>

#include <asm/irq.h>

/*
 * Interrupt enable mask
 * Bit N set means IRQ N is enabled
 */
static unsigned long irq_enable_mask = 0;

/*
 * do_IRQ - Main interrupt handler
 *
 * Called from assembly code when an interrupt occurs.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs *regs)
{
	struct pt_regs *oldregs;

	oldregs = set_irq_regs(regs);
	irq_enter();
	generic_handle_irq(irq);
	irq_exit();
	set_irq_regs(oldregs);
}

/*
 * chip_unmask - Enable an IRQ
 */
static void transputer_chip_unmask(struct irq_data *d)
{
	irq_enable_mask |= (1UL << d->irq);
}

/*
 * chip_mask - Disable an IRQ
 */
static void transputer_chip_mask(struct irq_data *d)
{
	irq_enable_mask &= ~(1UL << d->irq);
}

/*
 * IRQ chip definition
 */
static struct irq_chip transputer_irq_chip = {
	.name		= "TRANSPUTER-INTC",
	.irq_mask	= transputer_chip_mask,
	.irq_unmask	= transputer_chip_unmask,
};

/*
 * init_IRQ - Initialize the interrupt system
 *
 * Called during kernel boot to set up interrupts.
 */
void __init init_IRQ(void)
{
	int c;

	printk(KERN_INFO "Transputer: Initializing interrupt controller\n");

	for (c = 0; c < NR_IRQS; c++)
		irq_set_chip_and_handler(c, &transputer_irq_chip,
					 handle_simple_irq);

	irq_enable_mask = 0;

	printk(KERN_INFO "Transputer: IRQ system initialized (NR_IRQS=%d)\n",
	       NR_IRQS);
}
