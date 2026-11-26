# SPDX-License-Identifier: GPL-2.0
# Transputer kernel Makefile

obj-y := head.o init_task.o initcall_table.o main.o kernel_ref.o uart.o mmu.o console.o lib64.o smp_stubs.o stubs.o ffsll.o irq.o time.o mmio_iserver.o irqflags_stub.o setup.o process.o switch.o hw_spawn.o init.o exec.o flat_q3.o traps.o syscall.o syscall_entry.o tp_prot.o tp_goprot.o tp_net.o tp_input.o tp_sd.o tp_selftest.o fbtext.o

obj-$(CONFIG_TRANSPUTER_L_THDS_BOOT) += thds_boot.o thds_selth.o

obj-$(CONFIG_FB_SIMPLE) += simplefb.o

# Early console support
obj-$(CONFIG_EARLY_PRINTK) += early_printk.o

extra-y := vmlinux.lds

# Generate linker script
$(obj)/vmlinux.lds: $(src)/vmlinux.lds.S FORCE
	$(call if_changed_dep,cpp_lds_S)

obj-$(CONFIG_TRANSPUTER_SMP_BOOT) += tp_smp_boot.o tp_smp_entry.o

obj-$(CONFIG_SMP) += smp.o smp_locks.o
# NOMMU futex words; under PGM1 mm/pgm1.c walks the page tables
ifndef CONFIG_MMU
obj-$(CONFIG_SMP) += smp_futex.o
endif
obj-$(CONFIG_TRANSPUTER_PAGING_SELFTEST) += paging_selftest.o paging_load.o
obj-$(CONFIG_TRANSPUTER_PAGING_USER_SELFTEST) += pg_user_selftest.o
obj-$(CONFIG_MMU) += pgm1_user.o
