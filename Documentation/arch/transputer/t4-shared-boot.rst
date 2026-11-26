T4 shared-board secondary bootstrap
===================================

CONFIG_TRANSPUTER_T4_SMP_BOOT is an opt-in early architecture bring-up test.
The normal configuration leaves CONFIG_SMP disabled; see smp-build.rst for
the experimental SMP-enabled build baseline. CPU0 runs Linux; three secondary bootstrap
images execute the same admission/atomic test code, then park with STOPP.
They are not added to Linux's online mask and cannot run Linux tasks yet.

The shared boot ABI is in arch/transputer/include/asm/t4_smp_boot.h. CPU0
initializes the table at 0x40000000, publishes magic/version/count, and releases
CPUs 1..3. They enter using distinct 16 KiB workspaces in board RAM:

* CPU1: 0x40010000..0x40013fff
* CPU2: 0x40014000..0x40017fff
* CPU3: 0x40018000..0x4001bfff

Secondary text remains in private RAM. Each CPU reports its workspace,
identity cookie, loop count and error status. Stack-local arrays and bottom
canaries detect overlap/corruption. The boot table and workspaces are outside
Linux's private-RAM allocator; no shared region is registered with memblock.
Only CPU0 clears/publishes the boot table. Each secondary waits for release
before touching its record. Polling has finite iteration bounds and CPU0
panics on admission failure instead of continuing with a partial result.

All four CPUs perform 1000 shared fetch-adds, 1000 successful CAS increments
and 1000 exchange-lock-protected increments. CPU0 checks the three totals are
4000, checks failed-CAS semantics and each CPU record, publishes completion,
then waits for secondary parked acknowledgements. The normal kernel boot
continues only after T4_SHARED_BOOT: PASS.

The boot-only atomic wrapper orders compiler memory accesses around the
board command. Board serialization provides the actual cross-CPU ordering.
Per-port operand/command/result staging is safe here because this is an early
single-context entry with no installed interrupt handlers on the secondaries.
These helpers must not be used as general Linux arch_atomic_* operations:
local nesting/interrupt exclusion must be implemented first. The existing
uniprocessor Linux generic atomics are deliberately unchanged.

Reproduction in the TinyCC workspace
-----------------------------------

1. Build T4 with shared-board support and shared-workspace validation.
2. Build Linux with tpos_defconfig plus CONFIG_TRANSPUTER_T4_SMP_BOOT=y.
3. Run transputer/scripts/build-t4-secondary.sh OUTPUT, optionally setting
   TCC and NETLOAD to the same toolchain used for Linux.
4. Use four TP32X nodes (-s9 -st -sn -4 -sm 25) and the environment::

     T4_SHARED_BOARD=0:1,1:1,2:1,3:1
     T4_NODE_BTL=OUTPUT/secondary.btl
     SPYNET=network.map

   Keep these port assignments in CPU order. Reserve link 1 for the board;
   network.map can contain::

     Connect HOST to processor 0 link 0
     Connect processor 0 link 3 to processor 1 link 2
     Connect processor 1 link 3 to processor 2 link 2
     Connect processor 2 link 3 to processor 3 link 2

5. Boot CPU0 with -sb vmlinux.btl. Require all four T4 CPU records and the
   exact T4_SHARED_BOOT pass line, then check normal userspace boot.

On 19 November 2026 this passed with 32 MiB private RAM per node and a 16 MiB
shared board, followed in the same boot by protected pthread/libc tests,
epoll, POSIX timers, nested two-thread FP/MOVE2D signal state, two-context
CoreMark (4000 iterations total), and desktop/Files pixel verification.

Remaining SMP requirements
--------------------------

The kernel's mutable globals, allocator and task structures still reside in
CPU0's private RAM. A common kernel memory map and shared data/BSS loader
must precede general secondary kernel entry. current_thread_info and IRQ
mask state must become CPU-local; timer/trap/FP/MOVE2D state, idle tasks,
CPU-online sequencing, IPIs, cross-CPU rescheduling, spinlocks, atomic bitops
and barriers need architecture support and tests. The board is currently
data-only and has functional ordering without serial-link timing. Neither
this admission test nor two-context CoreMark demonstrates SMP scheduling or
physical-link speedup.
