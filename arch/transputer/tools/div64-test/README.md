# Transputer native division

`generate.py` emits a standalone C oracle with host-computed quotient and
remainder pairs. It covers nonzero divisors at both sides of the signed
boundary, high words at and around the divisor, and 1,024 random inputs.
Link the generated source, TinyCC's `transputer/tests/test_boot.S` with
`TEST_LARGE_WORKSPACE` defined, and the configured kernel's `div64.o`.
Run the resulting BTL under emulation. Success is `SDIV64 PASS` followed by `H`.
For smaller ISA targets, assemble `div64.S` for that target with the
kernel export stanza omitted.

The oracle passes on TP32I, TP32 and TP32X. The Linux `tpos_defconfig`
BTL boots with hard-P enabled, passes the protection self-test and
BusyBox listing checks, and reaches `DESKTOP_LOOP`. The captured
desktop framebuffer matches the baseline byte for byte.

The baseline profile records 1,401,927 modeled cycles over 205 calls
to `__div64_32`. The candidate records 18,217 cycles over 185 calls.
These boot samples contain different call counts. The corresponding
averages are 6,839 and 98 modeled cycles per call.
