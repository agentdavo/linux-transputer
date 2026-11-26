// SPDX-License-Identifier: GPL-2.0
#include <linux/futex.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <asm/cmpxchg.h>

static u32 *futex_pointer(u32 __user *uaddr)
{
    unsigned long la = (unsigned long)uaddr, pa;
    if ((la & 3) || !tp_soft_ok(tp_task_soft_mm(current), la, 4, 1)) return NULL;
    pa = (unsigned long)__tp_uptr(uaddr);
    if ((pa & 3) || !((pa >= 0x40000000u && pa <= 0x40fffffcu) ||
        (pa >= 0x80000000u && pa <= 0x80000000u + CONFIG_TRANSPUTER_RAM_MIB*1024u*1024u-4)))
        return NULL;
    return (u32 *)pa;
}
int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr, u32 oldval, u32 newval)
{
    u32 *p = futex_pointer(uaddr);
    if (!p) return -EFAULT;
    *uval = arch_cmpxchg(p, oldval, newval);
    return 0;
}
int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr)
{
    u32 *p = futex_pointer(uaddr), old, new, seen;
    if (!p) return -EFAULT;
    old = *(volatile u32 *)p;
    for (;;) {
        switch (op) {
        case FUTEX_OP_SET: new = oparg; break;
        case FUTEX_OP_ADD: new = old + oparg; break;
        case FUTEX_OP_OR: new = old | oparg; break;
        case FUTEX_OP_ANDN: new = old & ~oparg; break;
        case FUTEX_OP_XOR: new = old ^ oparg; break;
        default: return -ENOSYS;
        }
        seen = arch_cmpxchg(p, old, new);
        if (seen == old) { *oval = old; return 0; }
        old = seen;
    }
}

/* Early boot checks exercise the same hooks used by protected syscalls. */
int tp_smp_futex_selftest(void)
{
    u32 word = 7, observed;
    int old;
    if (futex_atomic_cmpxchg_inatomic(&observed, (u32 __user *)&word, 7, 9) || observed != 7 || word != 9) return -1;
    if (futex_atomic_cmpxchg_inatomic(&observed, (u32 __user *)&word, 7, 2) || observed != 9 || word != 9) return -2;
    if (arch_futex_atomic_op_inuser(FUTEX_OP_ADD, 3, &old, (u32 __user *)&word) || old != 9 || word != 12) return -3;
    if (arch_futex_atomic_op_inuser(FUTEX_OP_XOR, 5, &old, (u32 __user *)&word) || old != 12 || word != 9) return -4;
    if (arch_futex_atomic_op_inuser(99, 1, &old, (u32 __user *)&word) != -ENOSYS || word != 9) return -5;
    if (futex_atomic_cmpxchg_inatomic(&observed, (u32 __user *)1, 0, 1) != -EFAULT) return -6;
    return 0;
}
