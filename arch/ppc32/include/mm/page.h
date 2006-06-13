/*
 * Copyright (C) 2005 Martin Decky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /** @addtogroup ppc32mm	
 * @{
 */
/** @file
 */

#ifndef __ppc32_PAGE_H__
#define __ppc32_PAGE_H__

#include <arch/mm/frame.h>

#define PAGE_WIDTH	FRAME_WIDTH
#define PAGE_SIZE	FRAME_SIZE

#ifdef KERNEL

#ifndef __ASM__
#	define KA2PA(x)	(((__address) (x)) - 0x80000000)
#	define PA2KA(x)	(((__address) (x)) + 0x80000000)
#else
#	define KA2PA(x)	((x) - 0x80000000)
#	define PA2KA(x)	((x) + 0x80000000)
#endif

/*
 * Implementation of generic 4-level page table interface,
 * the hardware Page Hash Table is used as cache.
 *
 * Page table layout:
 * - 32-bit virtual addressess
 * - Offset is 12 bits => pages are 4K long
 * - PTL0 has 1024 entries (10 bits)
 * - PTL1 is not used
 * - PTL2 is not used
 * - PLT3 has 1024 entries (10 bits)
 */

#define PTL0_ENTRIES_ARCH	1024
#define PTL1_ENTRIES_ARCH	0
#define PTL2_ENTRIES_ARCH	0
#define PTL3_ENTRIES_ARCH	1024

#define PTL0_INDEX_ARCH(vaddr)	(((vaddr) >> 22) & 0x3ff)
#define PTL1_INDEX_ARCH(vaddr)	0
#define PTL2_INDEX_ARCH(vaddr)	0
#define PTL3_INDEX_ARCH(vaddr)	(((vaddr) >> 12) & 0x3ff)

#define GET_PTL1_ADDRESS_ARCH(ptl0, i)		(((pte_t *) (ptl0))[(i)].pfn << 12)
#define GET_PTL2_ADDRESS_ARCH(ptl1, i)		(ptl1)
#define GET_PTL3_ADDRESS_ARCH(ptl2, i)		(ptl2)
#define GET_FRAME_ADDRESS_ARCH(ptl3, i)		(((pte_t *) (ptl3))[(i)].pfn << 12)

#define SET_PTL0_ADDRESS_ARCH(ptl0)
#define SET_PTL1_ADDRESS_ARCH(ptl0, i, a)	(((pte_t *) (ptl0))[(i)].pfn = (a) >> 12)
#define SET_PTL2_ADDRESS_ARCH(ptl1, i, a)
#define SET_PTL3_ADDRESS_ARCH(ptl2, i, a)
#define SET_FRAME_ADDRESS_ARCH(ptl3, i, a)	(((pte_t *) (ptl3))[(i)].pfn = (a) >> 12)

#define GET_PTL1_FLAGS_ARCH(ptl0, i)		get_pt_flags((pte_t *) (ptl0), (index_t) (i))
#define GET_PTL2_FLAGS_ARCH(ptl1, i)		PAGE_PRESENT
#define GET_PTL3_FLAGS_ARCH(ptl2, i)		PAGE_PRESENT
#define GET_FRAME_FLAGS_ARCH(ptl3, i)		get_pt_flags((pte_t *) (ptl3), (index_t) (i))

#define SET_PTL1_FLAGS_ARCH(ptl0, i, x)		set_pt_flags((pte_t *) (ptl0), (index_t) (i), (x))
#define SET_PTL2_FLAGS_ARCH(ptl1, i, x)
#define SET_PTL3_FLAGS_ARCH(ptl2, i, x)
#define SET_FRAME_FLAGS_ARCH(ptl3, i, x)	set_pt_flags((pte_t *) (ptl3), (index_t) (i), (x))

#define PTE_VALID_ARCH(pte)			(*((__u32 *) (pte)) != 0)
#define PTE_PRESENT_ARCH(pte)			((pte)->p != 0)
#define PTE_GET_FRAME_ARCH(pte)			((pte)->pfn << 12)
#define PTE_WRITABLE_ARCH(pte)			1
#define PTE_EXECUTABLE_ARCH(pte)		1

#ifndef __ASM__

#include <mm/page.h>
#include <arch/mm/frame.h>
#include <arch/types.h>

static inline int get_pt_flags(pte_t *pt, index_t i)
{
	pte_t *p = &pt[i];
	
	return (
		(1 << PAGE_CACHEABLE_SHIFT) |
		((!p->p) << PAGE_PRESENT_SHIFT) |
		(1 << PAGE_USER_SHIFT) |
		(1 << PAGE_READ_SHIFT) |
		(1 << PAGE_WRITE_SHIFT) |
		(1 << PAGE_EXEC_SHIFT) |
		(p->g << PAGE_GLOBAL_SHIFT)
	);
}

static inline void set_pt_flags(pte_t *pt, index_t i, int flags)
{
	pte_t *p = &pt[i];
	
	p->p = !(flags & PAGE_NOT_PRESENT);
	p->g = (flags & PAGE_GLOBAL) != 0;
	p->valid = 1;
}

extern void page_arch_init(void);

#define PHT_BITS	16
#define PHT_ORDER	4

typedef struct {
	unsigned v : 1;          /**< Valid */
	unsigned vsid : 24;      /**< Virtual Segment ID */
	unsigned h : 1;          /**< Primary/secondary hash */
	unsigned api : 6;        /**< Abbreviated Page Index */
	unsigned rpn : 20;       /**< Real Page Number */
	unsigned reserved0 : 3;
	unsigned r : 1;          /**< Reference */
	unsigned c : 1;          /**< Change */
	unsigned wimg : 4;       /**< Access control */
	unsigned reserved1 : 1;
	unsigned pp : 2;         /**< Page protection */
} phte_t;

extern void pht_refill(int n, istate_t *istate);
extern void pht_init(void);

#endif /* __ASM__ */

#endif /* KERNEL */

#endif

 /** @}
 */

