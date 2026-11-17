#ifndef _ASM_TRANSPUTER_BARRIER_H
#define _ASM_TRANSPUTER_BARRIER_H

/* Transputer is single-core with no out-of-order execution,
   so barriers are compiler-only memory fences. */
#define __mb()    asm volatile("" ::: "memory")
#define __rmb()   __mb()
#define __wmb()   __mb()

#define read_barrier_depends()      do { } while (0)
#define set_mb(var, value)          do { (var) = (value); mb(); } while (0)

#include <asm-generic/barrier.h>

#endif /* _ASM_TRANSPUTER_BARRIER_H */
