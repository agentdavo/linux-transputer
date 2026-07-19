/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Transputer I/O memory access functions
 *
 * The Transputer uses memory-mapped I/O only (no I/O ports).
 * It has a flat memory model with no MMU, so ioremap is a no-op.
 */

#ifndef _ASM_TRANSPUTER_IO_H
#define _ASM_TRANSPUTER_IO_H

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>

extern void *memcpy(void *, const void *, __kernel_size_t);
extern void *memset(void *, int, __kernel_size_t);

/* PCI is not supported on Transputer */
#define IO_SPACE_LIMIT 0

/*
 * Basic MMIO read/write functions
 * Transputer uses memory-mapped I/O with little-endian byte order
 */
#define readb(addr) \
	({ unsigned char __v = (*(volatile unsigned char __force *)(addr)); __v; })

#define readw(addr) \
	({ unsigned short __v = (*(volatile unsigned short __force *)(addr)); __v; })

#define readl(addr) \
	({ unsigned int __v = (*(volatile unsigned int __force *)(addr)); __v; })

#define writeb(val, addr) \
	((void)(*(volatile unsigned char __force *)(addr) = (val)))

#define writew(val, addr) \
	((void)(*(volatile unsigned short __force *)(addr) = (val)))

#define writel(val, addr) \
	((void)(*(volatile unsigned int __force *)(addr) = (val)))

/*
 * Relaxed variants (no memory barriers)
 * On Transputer without cache coherency, same as normal versions
 */
#define readb_relaxed(addr)	readb(addr)
#define readw_relaxed(addr)	readw(addr)
#define readl_relaxed(addr)	readl(addr)

#define writeb_relaxed(val, addr)	writeb(val, addr)
#define writew_relaxed(val, addr)	writew(val, addr)
#define writel_relaxed(val, addr)	writel(val, addr)

/*
 * I/O memory mapping functions
 *
 * Transputer has no MMU, so ioremap is just an identity mapping.
 * Physical addresses are directly accessible.
 *
 * Don't define these as static inline - let asm-generic/io.h handle them
 * with the macros below.
 */
#define ioremap(addr, size)		((void __iomem *)(addr))
#define ioremap_nocache(addr, size)	((void __iomem *)(addr))
#define iounmap(addr)			do { } while (0)

/*
 * Memory barriers for I/O operations
 * Transputer needs barriers to ensure I/O ordering
 */
#define __io_before()	barrier()
#define __io_after()	barrier()

/*
 * Convert virtual addresses to physical and vice versa
 * Transputer uses flat memory model (no MMU), so these are identity mappings
 */
#define virt_to_phys(vaddr)	((unsigned long)(vaddr))
#define phys_to_virt(paddr)	((void *)(paddr))

/* /dev/mem and /dev/kmem address translation — identity on flat NOMMU */
#define xlate_dev_mem_ptr(p)	__va(p)
#define xlate_dev_kmem_ptr(p)	(p)

/*
 * Raw I/O variants (no byte-swap, no barriers)
 */
#define __raw_readb(addr)	readb(addr)
#define __raw_readw(addr)	readw(addr)
#define __raw_readl(addr)	readl(addr)
#define __raw_writeb(val, addr)	writeb(val, addr)
#define __raw_writew(val, addr)	writew(val, addr)
#define __raw_writel(val, addr)	writel(val, addr)

/*
 * Memory copy for I/O
 */
#define memcpy_fromio memcpy_fromio
static inline void memcpy_fromio(void *to, const volatile void __iomem *from,
				 unsigned long count)
{
	memcpy(to, (const void __force *)from, count);
}

#define memcpy_toio memcpy_toio
static inline void memcpy_toio(volatile void __iomem *to, const void *from,
			       unsigned long count)
{
	memcpy((void __force *)to, from, count);
}

#define memset_io memset_io
static inline void memset_io(volatile void __iomem *addr, unsigned char val,
			     unsigned long count)
{
	memset((void __force *)addr, val, count);
}

/*
 * I/O port access functions
 * Transputer has no I/O port space - these are not supported
 */
#define inb(port)		({ BUG(); 0; })
#define inw(port)		({ BUG(); 0; })
#define inl(port)		({ BUG(); 0; })
#define outb(val, port)		BUG()
#define outw(val, port)		BUG()
#define outl(val, port)		BUG()

#define inb_p(port)		inb(port)
#define inw_p(port)		inw(port)
#define inl_p(port)		inl(port)
#define outb_p(val, port)	outb(val, port)
#define outw_p(val, port)	outw(val, port)
#define outl_p(val, port)	outl(val, port)

/*
 * String I/O functions - not supported
 */
#define insb(port, addr, count)		BUG()
#define insw(port, addr, count)		BUG()
#define insl(port, addr, count)		BUG()
#define outsb(port, addr, count)	BUG()
#define outsw(port, addr, count)	BUG()
#define outsl(port, addr, count)	BUG()

#include <asm-generic/io.h>

#endif /* _ASM_TRANSPUTER_IO_H */
