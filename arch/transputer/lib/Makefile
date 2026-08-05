# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Transputer-specific library files.
#
# Note: memset, memcpy, memmove are implemented in assembly to avoid
# TCC miscompiling the while(count--) loop pattern.
#

# memcpy.o must be linked even though asm/string.h defines memcpy as a static
# inline: TCC lowers some constructs (struct assignment, __builtin_memcpy) to a
# real call, so the symbol is referenced regardless.  Without it the link left
# `memcpy` undefined - which used to be silent and is now a warning - and every
# such site became a wild call.
lib-y	:= delay.o string.o strcpy.o page.o memcpy.o

# Force assembly versions - the .S files will be assembled instead of .c
# Make sure to remove or rename the .c files to avoid conflicts
