#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Generate a standalone target test with host-computed quotient/remainder."""

from random import Random

print("""extern unsigned int __div64_32(unsigned long long *, unsigned int);
struct trial {
    unsigned long long dividend, quotient;
    unsigned int divisor, remainder;
};
static const struct trial trials[] = {""")

rng = Random(329)
edges = (0, 1, 2, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff)


def entry(high, low, divisor):
    dividend = (high << 32) | low
    q, r = divmod(dividend, divisor)
    print(f"    {{0x{dividend:016x}ull, 0x{q:016x}ull, {divisor}u, {r}u}},")


for divisor in edges[1:]:
    for high in set(edges + (divisor - 1, divisor, min(divisor + 1, edges[-1]))):
        for low in edges:
            entry(high, low, divisor)
for _ in range(1024):
    entry(rng.getrandbits(32), rng.getrandbits(32), rng.randrange(1, 1 << 32))

print("""};

void test_main(void)
{
    volatile unsigned int *uart = (volatile unsigned int *)0x10000000u;
    const char *msg = "DIV64 PASS\\n";
    unsigned int i;

    for (i = 0; i < sizeof(trials) / sizeof(trials[0]); i++) {
        const struct trial *t = trials + i;
        unsigned long long n = t->dividend;
        unsigned int remainder = __div64_32(&n, t->divisor);

        if (n != t->quotient || remainder != t->remainder) {
            msg = "DIV64 FAIL\\n";
            break;
        }
    }
    while (*msg)
        *uart = *msg++;
}""")
