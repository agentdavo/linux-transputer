# 1 "arch/transputer/kernel/vmlinux.lds.S"
# 1 "<command line>" 1
# 32 "<command line>"
# 1 "./include/linux/compiler-version.h" 1
# 32 "<command line>" 2

# 1 "./include/linux/kconfig.h" 1





# 1 "./include/generated/autoconf.h" 1
# 6 "./include/linux/kconfig.h" 2
# 33 "<command line>" 2
# 1 "arch/transputer/kernel/vmlinux.lds.S" 2







# 1 "./include/asm-generic/vmlinux.lds.h" 1
# 54 "./include/asm-generic/vmlinux.lds.h"
# 1 "./include/asm-generic/codetag.lds.h" 1
# 54 "./include/asm-generic/vmlinux.lds.h" 2
# 8 "arch/transputer/kernel/vmlinux.lds.S" 2

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
		. = ALIGN(32); __sched_class_highest = .; *(__stop_sched_class) *(__dl_sched_class) *(__rt_sched_class) *(__fair_sched_class) *(__ext_sched_class) *(__idle_sched_class) __sched_class_lowest = .;
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
