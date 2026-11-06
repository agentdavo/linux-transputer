#!/usr/bin/env python3
"""Check a trap-first desktop boot using its UART file and T4_PROTECT_DEBUG trace."""

import argparse
import re
from pathlib import Path


def check(uart, trace):
    if uart.count("FIND_APPLET:true\n") != 1:
        raise ValueError("expected exactly one execution of the true applet")

    begin = trace.index("PID1:spawn-true-test")
    dispatch = trace.index("SPAWN:/bin/true\n", begin)
    trap = re.search(r"P-trap cause=3 Eptr=0x([0-9A-Fa-f]+)", trace[begin:dispatch])
    if not trap:
        raise ValueError("PID 1 did not enter the supervisor through syscall")

    returned = trace.index("SPAWN_RET:00000000", dispatch)
    continued = trace.index("PID1:true-test-ret=00000000", returned)
    resume = re.search(r"goprot [^\n]*-> P IPtr=0x([0-9A-Fa-f]+)",
                       trace[returned:continued])
    # The syscall stub emits NFIX 0; OPR 8, exactly two bytes.
    next_ip = int(trap[1], 16) + 2
    if not resume or int(resume[1], 16) != next_ip:
        raise ValueError("first goprot after child exit did not resume the caller")

    status = uart.index("PID1:true-test-ret=00000000")
    shell = uart.index("PID1:sh-spawn-ret=00000000", status)
    uart.index("DESKTOP_LOOP", shell)
    return next_ip


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("uart", type=Path)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        ip = check(args.uart.read_text(encoding="latin1"),
                   args.trace.read_text(encoding="latin1"))
    except ValueError as exc:
        parser.exit(1, f"FAIL: {exc}\n")
    print(f"PASS: one true, PID 1 resumed at {ip:#010x}, desktop reached")
