#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Build the console pixel oracle with an already compiled kernel fbtext.o.
set -euo pipefail
tools=$(realpath "${1:?expected TinyCC toolchain directory}")
kernel=$(realpath "${2:?expected kernel build directory}")
out=$(realpath -m "${3:?expected output directory}")
src=$(dirname "$(realpath "$0")")
cc=${TCC_BIN:-$tools/transputer-tcc}
netload=${NETLOAD_BIN:-$tools/transputer-netload}
mkdir -p "$out"
TP_STRICT_UNRESOLVED_CALLS=1 "$cc" -mtp32x -nostdlib \
    -DTEST_LARGE_WORKSPACE -o "$out/test.elf" \
    "$tools/transputer/tests/test_boot.S" "$src/test.c" \
    "$kernel/arch/transputer/kernel/fbtext.o"
"$netload" "$out/test.elf" "$out/test.btl"
