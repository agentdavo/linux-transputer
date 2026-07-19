#ifndef __ASM_BUG_H
#define __ASM_BUG_H

#include <linux/compiler.h>

#define BUG() do { \
    const char *__transputer_bug_file = __FILE__; \
    unsigned int __transputer_bug_line = __LINE__; \
    int __transputer_bug_shift; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'!'; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'B'; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'U'; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'G'; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)' '; \
    while (*__transputer_bug_file) \
        *(volatile unsigned int *)0x10000000u = \
            (unsigned int)(unsigned char)*__transputer_bug_file++; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)':'; \
    for (__transputer_bug_shift = 12; __transputer_bug_shift >= 0; \
         __transputer_bug_shift -= 4) { \
        unsigned int __transputer_bug_digit = \
            (__transputer_bug_line >> __transputer_bug_shift) & 0xfu; \
        *(volatile unsigned int *)0x10000000u = \
            (unsigned int)(__transputer_bug_digit < 10u \
                ? '0' + __transputer_bug_digit \
                : 'a' + __transputer_bug_digit - 10u); \
    } \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'\r'; \
    *(volatile unsigned int *)0x10000000u = (unsigned int)'\n'; \
    for(;;) ; \
} while (0)

#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while (0)

#define WARN_ON(condition) ({ \
    int __ret_warn_on = !!(condition); \
    if (unlikely(__ret_warn_on)) \
        __asm__ __volatile__(".byte 0x00"); /* Warning marker */ \
    unlikely(__ret_warn_on); \
})

#define HAVE_ARCH_BUG
#define HAVE_ARCH_BUG_ON
#define HAVE_ARCH_WARN_ON

#include <asm-generic/bug.h>

#endif /* __ASM_BUG_H */
