#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <linux/compiler.h>

/* UART: !BUG + 4 hex __LINE__ digits.  Halt via stopp (0x21F5), not
 * for(;;) — TCC has miscompiled empty infinite loops into fall-through. */
#define BUG() do { \
    unsigned int __bug_line = (unsigned int)__LINE__; \
    int __bug_i; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'!'; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'B'; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'U'; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'G'; \
    for (__bug_i = 12; __bug_i >= 0; __bug_i -= 4) { \
        unsigned int __d = (__bug_line >> __bug_i) & 0xfu; \
        *(volatile unsigned int *)0x10000000u = \
            (unsigned int)(__d < 10u ? '0' + __d : 'a' + __d - 10u); \
    } \
    __asm__ __volatile__(".byte 0x21, 0xF5"); /* stopp */ \
    for (;;) \
        __asm__ __volatile__(""); \
} while (0)

#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while (0)

/* Do not emit 0x00 (illegal) — that G/L-traps mid-boot under SPINE. */
#define WARN_ON(condition) ({ \
    int __ret_warn_on = !!(condition); \
    if (unlikely(__ret_warn_on)) \
        *(volatile unsigned int *)0x10000000u = (unsigned int)'w'; \
    unlikely(__ret_warn_on); \
})

#define HAVE_ARCH_BUG
#define HAVE_ARCH_BUG_ON
#define HAVE_ARCH_WARN_ON

#include <asm-generic/bug.h>

#endif /* __ASM_BUG_H */
