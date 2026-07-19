# SPDX-License-Identifier: GPL-2.0
#
# arch/transputer/boot/Makefile
#

targets := vmlinux.bin

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

install:
	@echo "Installing kernel for transputer..."

clean: