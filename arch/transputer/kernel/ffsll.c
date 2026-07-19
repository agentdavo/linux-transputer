/* Provide ffsll / __builtin_ffsll implementation.
 *
 * TCC needs a separate stub + asm alias because naming a function
 * __builtin_* directly conflicts with TCC internals.
 * GCC 3.2.3 doesn't have __builtin_ffsll for transputer, so we
 * provide it directly (the t800 backend prepends _ to C names,
 * so this becomes the _ffsll symbol).
 */

#ifdef __TINYC__

int ffsll_stub(unsigned long long x) {
    int n;
    unsigned long long mask;
    if (!x) return 0;
    n = 1;
    mask = 1;
    while (!(x & mask)) {
        mask <<= 1;
        n++;
    }
    return n;
}

/* Use asm directive to create the __builtin_ffsll symbol (TCC only) */
asm(".globl __builtin_ffsll\n"
    "__builtin_ffsll = ffsll_stub\n");

#else /* GCC */

int ffsll(unsigned long long x) {
    int n;
    unsigned long long mask;
    if (!x) return 0;
    n = 1;
    mask = 1;
    while (!(x & mask)) {
        mask <<= 1;
        n++;
    }
    return n;
}

#endif
