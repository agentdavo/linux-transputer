// SPDX-License-Identifier: GPL-2.0
/*
 * Transputer memory management fault handling
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long address)
{
	struct vm_area_struct *vma = NULL;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	int code = SEGV_MAPERR;
	vm_fault_t fault;
	unsigned int flags = user_mode(regs) ? FAULT_FLAG_USER : 0;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (in_atomic() || !mm)
		goto bad_area_nosemaphore;


retry:
	vma = lock_mm_and_find_vma(mm, address, regs);
	if (!vma)
		goto bad_area_nosemaphore;

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
	code = SEGV_ACCERR;

	if (!(vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)))
		goto bad_area;

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(vma, address, flags, regs);

	if (fault_signal_pending(fault, regs)) {
		if (!user_mode(regs))
			goto no_context;
		return;
	}
	if (fault & VM_FAULT_COMPLETED)
		return;

	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}
	if (fault & VM_FAULT_RETRY) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}

	if (fault & VM_FAULT_MAJOR)
		tsk->maj_flt++;
	else
		tsk->min_flt++;

	mmap_read_unlock(mm);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	mmap_read_unlock(mm);

bad_area_nosemaphore:
	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs)) {
		if (unhandled_signal(current, SIGSEGV) && printk_ratelimit()) {
			pr_info("%s: unhandled page fault (%d) at 0x%08lx\n",
				current->comm, SIGSEGV, address);
			show_regs(regs);
		}
		force_sig_fault(SIGSEGV, code, (void __user *)address);
		return;
	}

no_context:
	/* Are we prepared to handle this kernel fault? */
	if (fixup_exception(regs))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	bust_spinlocks(1);

	pr_alert("Unable to handle kernel %s at virtual address %08lx",
		address < PAGE_SIZE ? "NULL pointer dereference" :
		"paging request", address);
	pr_alert("iptr = %08lx\n", regs->iptr);
	panic("Oops");
	return;

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	mmap_read_unlock(mm);
	if (!user_mode(regs))
		goto no_context;
	pagefault_out_of_memory();
	return;

do_sigbus:
	mmap_read_unlock(mm);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;

	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);
	return;
}
