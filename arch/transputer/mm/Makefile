# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Transputer-specific parts of the memory manager.
#
obj-y := init.o
obj-y += extable.o
obj-y += fault.o
obj-y += tcc_stubs.o
ifdef CONFIG_MMU
obj-y += pgm1.o
else
obj-y += uaccess.o
endif
