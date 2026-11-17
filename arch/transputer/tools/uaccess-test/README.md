# Protected user-string regression test

This bare-metal test links the configured kernel's actual `uaccess.o`.
It uses the real soft-protection definitions, a synthetic current task,
and a 256-byte region remapped from logical Q3 to physical RAM.

Cases cover NUL-terminated strings at region, private-image, disjoint-stack
and anonymous-mapping boundaries; truncated and unterminated strings;
invalid quarters and NULL; zero or negative counts; the final address in
the 32-bit address space; wraparound; and inactive identity mapping.
The synthetic mmap callback only admits the test's explicit mapping.

```sh
TCC_BIN=/path/to/transputer-tcc NETLOAD_BIN=/path/to/transputer-netload \
    bash transputer/linux-7/arch/transputer/tools/uaccess-test/build.sh \
    "$PWD" /path/to/kernel-build /path/to/test-output
cd /path/to/test-output
T4_HEADLESS=1 T4_UART_LOG=uart \
    /path/to/t4 -s9 -sm 25 -ss -sb test.btl
```

Success is `SUSERSTRINGS PASS`, followed by the bootstrap's `H` marker.
The fixture uses the configured kernel's headers and the retained TinyCC
GCC 12.2 facade, matching the Linux 7 build.

## Validation on 17 November 2026

Linking the same fixture against the previous generic `strncpy_from_user.o`
and `strnlen_user.o` reports ten failures. With the architecture routines,
all cases pass. The fix checks one byte before translating and reading it,
so a large pathname limit no longer rejects a short valid string, while
unterminated strings still fail at the first inaccessible byte.

The rebuilt `tpos_defconfig` BTL boots under T4 `-s806 -sm 25` with hard-P
enabled, passes the protection self-test, and reaches `DESKTOP_LOOP`.
The existing BusyBox startup tests now list `/tmp`, `/bin`, and `/` with
return value zero; each previously failed with `EFAULT`. The actual desktop
framebuffer remains byte-identical, SHA-256
`0859bf0f8ef7b7fa269f0d67644dbb77f6f57f5fa8aca5967ff782c9c1ac616e`.
The strict link checks 81,401 call relocations with zero mismatches.
The existing three modpost section-mismatch warnings remain.

BTL SHA-256:
`ebc3514895743e525a254a7917dab456063c0ca29cb89fdce99feb70f69077c4`.
