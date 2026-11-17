#
# arch/transputer/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

KBUILD_DEFCONFIG	:= tpos_defconfig

# Compiler selection: USE_GCC=1 for GCC 3.2.3 transputer backend
ifeq ($(USE_GCC),1)
CC := $(srctree)/../transputer-gcc
KBUILD_CFLAGS := -O2 -ffreestanding -nostdinc
else
# Default to the TinyCC cross compiler if none is specified
ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := transputer-
endif

# Override CC: use transputer-tcc instead of transputer-gcc
CC := $(CROSS_COMPILE)tcc

# Safety check: Verify we're using TCC wrappers, not GCC
ifeq ($(CONFIG_CC_VERSION_TEXT),)
$(warning CONFIG_CC_VERSION_TEXT not set - are you using the TCC wrappers?)
$(warning Build the kernel with: ./transputer/build.sh --kernel)
endif

# Kernel compilation flags
# Note: -Greduced removed (MIPS-style flag, not supported by TCC)
endif

head-y				+= arch/transputer/kernel/head.o
core-y				+= arch/transputer/kernel/
core-y				+= arch/transputer/mm/
libs-y				+= arch/transputer/lib/

# Linker script
export LDFLAGS_vmlinux := -T arch/transputer/kernel/vmlinux.lds

boot := arch/transputer/boot

all: vmlinux

PHONY += archheaders archscripts

define archhelp
  echo  '* vmlinux      - Uncompressed kernel image (arch/transputer/boot/vmlinux)'
endef
