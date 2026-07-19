# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Transputer-specific library files.
#
# Note: memset, memcpy, memmove are implemented in assembly to avoid
# TCC miscompiling the while(count--) loop pattern.
#

lib-y	:= delay.o string.o strcpy.o page.o

# Force assembly versions - the .S files will be assembled instead of .c
# Make sure to remove or rename the .c files to avoid conflicts
