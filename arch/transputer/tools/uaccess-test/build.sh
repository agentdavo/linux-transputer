#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Link the configured kernel's actual uaccess object into a bare-metal test.
set -euo pipefail
tools=$(realpath "${1:?expected TinyCC toolchain directory}")
kernel=$(realpath "${2:?expected kernel build directory}")
out=$(realpath -m "${3:?expected output directory}")
src=$(dirname "$(realpath "$0")")
cc=${TCC_BIN:-$tools/transputer-tcc}
netload=${NETLOAD_BIN:-$tools/transputer-netload}
mkdir -p "$out"
T4_STRICT_UNRESOLVED_CALLS=1 "$cc" -mt9000 -nostdlib -nostdinc \
    -DTEST_LARGE_WORKSPACE -D__KERNEL__ \
    -D__GNUC__=12 -D__GNUC_MINOR__=2 -D__GNUC_PATCHLEVEL__=0 \
    -I"$kernel/arch/transputer/include" \
    -I"$kernel/arch/transputer/include/generated" \
    -I"$kernel/include" -I"$kernel/include/generated" \
    -I"$kernel/arch/transputer/include/uapi" \
    -I"$kernel/arch/transputer/include/generated/uapi" \
    -I"$kernel/include/uapi" -I"$kernel/include/generated/uapi" \
    -I"$tools" -I"$tools/include" \
    -include "$kernel/include/linux/kconfig.h" \
    -o "$out/test.elf" "$tools/transputer/tests/test_boot.S" \
    "$src/test.c" "$kernel/arch/transputer/mm/uaccess.o"
"$netload" "$out/test.elf" "$out/test.btl"
