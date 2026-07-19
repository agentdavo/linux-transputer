# SPDX-License-Identifier: GPL-2.0
# Transputer kernel Makefile

obj-y := head.o init_task.o initcall_table.o main.o kernel_ref.o uart.o mmu.o console.o lib64.o smp_stubs.o stubs.o ffsll.o irq.o time.o mmio_iserver.o irqflags_stub.o setup.o process.o switch.o hw_spawn.o init.o exec.o traps.o syscall.o syscall_entry.o t4net.o t4_input.o t4_sd.o t4_selftest.o

obj-$(CONFIG_FB_SIMPLE) += simplefb.o

# Early console support
obj-$(CONFIG_EARLY_PRINTK) += early_printk.o

extra-y := vmlinux.lds

# Generate linker script
$(obj)/vmlinux.lds: $(src)/vmlinux.lds.S FORCE
	$(call if_changed_dep,cpp_lds_S)
