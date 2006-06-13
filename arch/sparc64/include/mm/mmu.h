/*
 * Copyright (C) 2005 Jakub Jermar
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

 /** @addtogroup sparc64mm	
 * @{
 */
/** @file
 */

#ifndef __sparc64_MMU_H__
#define __sparc64_MMU_H__

#include <arch/asm.h>
#include <arch/barrier.h>
#include <arch/types.h>
#include <typedefs.h>

/** LSU Control Register ASI. */
#define ASI_LSU_CONTROL_REG		0x45	/**< Load/Store Unit Control Register. */

/** I-MMU ASIs. */
#define ASI_IMMU			0x50
#define ASI_IMMU_TSB_8KB_PTR_REG	0x51	
#define ASI_IMMU_TSB_64KB_PTR_REG	0x52
#define ASI_ITLB_DATA_IN_REG		0x54
#define ASI_ITLB_DATA_ACCESS_REG	0x55
#define ASI_ITLB_TAG_READ_REG		0x56
#define ASI_IMMU_DEMAP			0x57

/** Virtual Addresses within ASI_IMMU. */
#define VA_IMMU_TAG_TARGET		0x0	/**< IMMU tag target register. */
#define VA_IMMU_SFSR			0x18	/**< IMMU sync fault status register. */
#define VA_IMMU_TSB_BASE		0x28	/**< IMMU TSB base register. */
#define VA_IMMU_TAG_ACCESS		0x30	/**< IMMU TLB tag access register. */

/** D-MMU ASIs. */
#define ASI_DMMU			0x58
#define ASI_DMMU_TSB_8KB_PTR_REG	0x59	
#define ASI_DMMU_TSB_64KB_PTR_REG	0x5a
#define ASI_DMMU_TSB_DIRECT_PTR_REG	0x5b
#define ASI_DTLB_DATA_IN_REG		0x5c
#define ASI_DTLB_DATA_ACCESS_REG	0x5d
#define ASI_DTLB_TAG_READ_REG		0x5e
#define ASI_DMMU_DEMAP			0x5f

/** Virtual Addresses within ASI_DMMU. */
#define VA_DMMU_TAG_TARGET		0x0	/**< DMMU tag target register. */
#define VA_PRIMARY_CONTEXT_REG		0x8	/**< DMMU primary context register. */
#define VA_SECONDARY_CONTEXT_REG	0x10	/**< DMMU secondary context register. */
#define VA_DMMU_SFSR			0x18	/**< DMMU sync fault status register. */
#define VA_DMMU_SFAR			0x20	/**< DMMU sync fault address register. */
#define VA_DMMU_TSB_BASE		0x28	/**< DMMU TSB base register. */
#define VA_DMMU_TAG_ACCESS		0x30	/**< DMMU TLB tag access register. */
#define VA_DMMU_VA_WATCHPOINT_REG	0x38	/**< DMMU VA data watchpoint register. */
#define VA_DMMU_PA_WATCHPOINT_REG	0x40	/**< DMMU PA data watchpoint register. */


/** LSU Control Register. */
union lsu_cr_reg {
	__u64 value;
	struct {
		unsigned : 23;
		unsigned pm : 8;
		unsigned vm : 8;
		unsigned pr : 1;
		unsigned pw : 1;
		unsigned vr : 1;
		unsigned vw : 1;
		unsigned : 1;
		unsigned fm : 16;	
		unsigned dm : 1;	/**< D-MMU enable. */
		unsigned im : 1;	/**< I-MMU enable. */
		unsigned dc : 1;	/**< D-Cache enable. */
		unsigned ic : 1;	/**< I-Cache enable. */
		
	} __attribute__ ((packed));
};
typedef union lsu_cr_reg lsu_cr_reg_t;


#define immu_enable()	immu_set(true)
#define immu_disable()	immu_set(false)
#define dmmu_enable()	dmmu_set(true)
#define dmmu_disable()	dmmu_set(false)

/** Disable or Enable IMMU. */
static inline void immu_set(bool enable)
{
	lsu_cr_reg_t cr;
	
	cr.value = asi_u64_read(ASI_LSU_CONTROL_REG, 0);
	cr.im = enable;
	asi_u64_write(ASI_LSU_CONTROL_REG, 0, cr.value);
	membar();
}

/** Disable or Enable DMMU. */
static inline void dmmu_set(bool enable)
{
	lsu_cr_reg_t cr;
	
	cr.value = asi_u64_read(ASI_LSU_CONTROL_REG, 0);
	cr.dm = enable;
	asi_u64_write(ASI_LSU_CONTROL_REG, 0, cr.value);
	membar();
}

#endif

 /** @}
 */

