#ifndef _ASM_TRANSPUTER_BARRIER_H
#define _ASM_TRANSPUTER_BARRIER_H

/* The platform executes accesses synchronously; the shared board serializes each
 * transaction. Compiler fences suffice for this functional model only.
 * A queued/timed transport must implement completion fences here. */
#define __mb()    asm volatile("" ::: "memory")
#define __rmb()   __mb()
#define __wmb()   __mb()

#define read_barrier_depends()      do { } while (0)
#define set_mb(var, value)          do { (var) = (value); mb(); } while (0)

#include <asm-generic/barrier.h>

#endif /* _ASM_TRANSPUTER_BARRIER_H */
