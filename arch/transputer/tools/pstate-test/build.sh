#!/bin/bash
# Build a trap-first P-state test overlay. Pass a built tinycc_transputer
# tree (with the per-call stub) and a new output directory, then overlay
# its init and usr/bin files on a bootable rootfs before packing the CPIO.
# The UART must show one P-STATE-PASS for roles 2, 1, 0, in that order,
# PID1:pstate-ret=00000017, the shell returning zero, and DESKTOP_LOOP.
set -euo pipefail
tools=$(realpath "${1:?expected toolchain tree and new output directory}")
out=$(realpath -m "${2:?expected a new output directory}")
src=$(dirname "$(realpath "$0")")
mkdir "$out"
mkdir -p "$out/usr/bin"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
init="$tools/transputer/initramfs"
# Insert the extra spawn into the normal PID 1 smoke tests.
python3 - "$init/init.c" "$work/init.c" <<'PY'
from pathlib import Path
import sys
s = Path(sys.argv[1]).read_text()
anchor = 'log_tag("spawn-uname-test");'
if s.count(anchor) != 1:
    sys.exit("expected one PID 1 smoke-test insertion point")
s = s.replace(anchor, '{ extern long pstate_probe(char **); '
              'log_value("pstate-ret", pstate_probe(0)); }\n' + anchor)
Path(sys.argv[2]).write_text(s)
PY
for role in 0 1 2; do
	input=("$init/crt1.S" "$tools/transputer/tests/syscall_stub.c" "$src/state.S" "$src/probe.c")
	case "$role" in
		0) file=init; input+=("$work/init.c") ;;
		1) file=usr/bin/p-child ;;
		2) file=usr/bin/p-grandchild ;;
	esac
	"$tools/transputer-tcc" -DROLE="$role" -DTP_USE_T9K_SYSCALL=1 -nostdlib -nostdinc \
		-o "$work/probe.elf" "${input[@]}" "$tools/transputer-libtcc1.a"
	"$tools/transputer-elf2flt" -k "$work/probe.elf" "$out/$file"
	chmod 755 "$out/$file"
done
