/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal linker script for Transputer bring-up.
 * The current ld wrapper ignores -T, but Kbuild still expects this file.
 */

#include <asm-generic/vmlinux.lds.h>

ENTRY(_start)

SECTIONS
{
	. = 0x80000070;

	.text : {
		_text = .;
		_stext = .;
		*(.text.start)
		*(.text)
		*(.text.*)
		_etext = .;
	}

	.rodata : {
		*(.rodata)
		*(.rodata.*)
		SCHED_DATA
	}

	.data : {
		_data = .;
		*(.data)
		*(.data.*)
		_edata = .;
	}

	.init.ramfs : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
		*(.init.ramfs.info)
	}

	.bss : {
		__bss_start = .;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		__bss_stop = .;
	}

	_end = .;
}
