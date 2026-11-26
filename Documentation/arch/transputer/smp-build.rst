Six-CPU T4 shared-kernel SMP
===========================

``tpos_smp_defconfig`` enables CONFIG_SMP, six possible CPUs,
CONFIG_GENERIC_SMP_IDLE_THREAD and CONFIG_TRANSPUTER_T4_SHARED_KERNEL.
This targets a custom T4 machine with six data links per TP32X; physical
TP32X silicon has four data links and two separate control links.

Machine configuration
---------------------

Run T4 with ``-s9 -st -sn -6 -sm 25 -sb vmlinux.btl`` and::

  T4_DATA_LINKS=6
  T4_SHARED_KERNEL=1
  T4_SHARED_BOARD=0:5,1:5,2:5,3:5,4:5,5:5
  SPYNET=/absolute/path/to/t4/tests/six_cpu_mesh.net

Do not set T4_NODE_BTL. CPU0 loads one image; secondary CPUs enter that
image through release mailboxes without clearing its BSS. Mesh links 0--4
connect each CPU to its five peers, and link 5 attaches to the board.
Control links remain separate. Six-link mode uses UART instead of host
services on data link 0.

The first 112 bytes of the high memory map remain CPU-local hardware state.
RAM from 0x80000070 through the configured RAM limit is common kernel and
allocator memory. A 32 MiB configuration is one common 32 MiB Linux pool,
not six separate Linux pools. The positive board window is separate.

CPU identity, IRQ masks, current-task pointers, THDS descriptors and timer
state are CPU-local. Generic Linux creates an idle task and workspace for
each secondary. Direct secondary entry clears the architecture's new-task
flag so subsequent idle resumes restore the saved context. It also starts
the local hardware timers explicitly, since this entry bypasses head.S.
/proc/cpuinfo enumerates each online CPU.

Synchronization and IPIs
------------------------

Byte, halfword and word exchange/compare-exchange, spinlocks, reader/writer
locks and protected futex hooks use serialized board CAS for shared RAM.
Masked subword retries preserve neighbouring fields. Local IRQ exclusion
covers each per-port operand/command/result sequence. Contended locks poll
IPIs only when IRQs are enabled. Mapping bitmap metadata has its own
IRQ-safe lock.

Shared mailboxes carry reschedule, call-function and stop requests. Delivery
occurs at polling boundaries with IRQ accounting; arbitrary L-mode kernel
loops are not asynchronously interrupted. The initial userspace policy pins
processes to CPU0 because legacy/protected syscall bookkeeping is not yet
safe for migration. Kernel workers can run on all online CPUs.

The shared-kernel configuration enables NO_HZ_IDLE and supplies a real
1 MHz shared clocksource from the beginning of timekeeping. Each CPU has
a polled one-shot clockevent. Only an armed ONESHOT device dispatches;
shutdown, detached and oneshot-stopped states cannot replay periodic ticks.
Linux's generic tickless-idle code manages elapsed time and idle accounting.
The legacy non-shared configuration retains its periodic LDTIMER path.

Idle CPUs write the board idle-hint register at 0x42000010. The controller
checks the pending IPI and waits under the same mutex used by publishers,
so an IPI cannot be lost between the check and sleep. CPU0 retains its
one-millisecond peripheral polling interval. Secondary CPUs request at most
50 milliseconds, shortened to any armed timer deadline. An IPI interrupts
the wait immediately; the hint does not clear it. Host scheduling and long
kernel sections can delay actual event dispatch beyond the deadline.
This requires T4 commit d9a8fd1 or later for adaptive waits; older controllers
retain their fixed one-millisecond wait and therefore lose the idle speedup.

Board access is a functional serialized model, not a calibrated simulation
of six memory links, coherent caches or memory-controller bandwidth. Mesh
channel traffic is tested separately from shared-memory IPI traffic.

Compiler and assembly requirements
----------------------------------

The TinyCC linker must group all .data..percpu subsections, materialize
zero-fill inputs and provide __per_cpu_start/end/load and lock-text bounds.
It must also include the constant-address-lvalue correction: MMIO values
must not be folded as address literals during integer operations.

The T4 SELTH wrapper loads its argument from W[1]. Shared-kernel builds may
select the SPINE device profile, but execute the normal T4 CALL-frame ABI;
they must not use the physical SPINE register-argument convention.

Validation
----------

Boot selftests check futex atomic operations, synchronous callbacks on each
online CPU, hardware/task CPU identity, and independent per-CPU counters.
A later test starts one bound kernel worker per online CPU, each performing
128 allocation/write/check/free iterations with a shared atomic count.
All workers report ready before a common completion releases them.
The six-CPU target expects 768 completed allocations.

The 19 November tickless-idle candidate passed, in one 32 MiB six-CPU boot:
atomic/futex tests, cross-CPU callbacks, the common six-worker start barrier,
768 worker allocations, protection selftest, libc malloc/stdio stress,
epoll, POSIX timers, CPU-bound and nested two-thread signal handlers with
FP/MOVE2D state, two-context CoreMark, and desktop/Files/desktop restoration.
All six entries were read from /proc/cpuinfo. The clockevent target selftest
covers wraparound, exact expiry, consume-once, rearming, disarming and
per-device CPU selection. The legacy timer/process objects also compile
with the non-SMP configuration.

The periodic baseline exhausted its 900-second integration deadline during
libc stress. Measurements exposed repeated capped 16-tick batches and almost
no idle waiting. With NO_HZ_IDLE plus adaptive waits, all five idle APs
completed zero timer events in a 30.35-second sample and spent 27.08--29.47
seconds waiting. These are emulator diagnostics, not physical TP32X timing
or a six-core userspace speedup. CoreMark remains two contexts pinned to CPU0;
its 4,000 iterations validated in 387.063040 seconds in this run.

The framebuffer captures were visually checked. Files opens correctly and
closing it restores exactly the original desktop outside the clock area.
The full validation runner is transputer/tests/test_smp_timer_desktop.py in
the surrounding workspace; it requires a prepared protected test initramfs,
Xvfb and xdotool. Its diagnostics pause the emulator briefly for snapshots.

The surrounding TinyCC workspace holds the integration checkpoint at
transputer/tests/bench/six-cpu-smp-2026-11-19.md. Consult it for the exact
validated images and remaining limitations; a successful emulator mesh test
alone does not establish a successful Linux workload run.
