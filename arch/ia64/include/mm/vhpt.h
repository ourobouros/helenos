/*
* Copyright (C) 2006 Jakub Vana
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

 /** @addtogroup ia64mm	
 * @{
 */
/** @file
*/
													

#ifndef __ia64_VHPT_H__
#define __ia64_VHPT_H__

#include <arch/mm/tlb.h>
#include <arch/mm/page.h>

__address vhpt_set_up(void);

static inline vhpt_entry_t tlb_entry_t2vhpt_entry_t(tlb_entry_t tentry) 
{
	vhpt_entry_t ventry;
	
	ventry.word[0]=tentry.word[0];
	ventry.word[1]=tentry.word[1];
	
	return ventry;
}

void vhpt_mapping_insert(__address va, asid_t asid, tlb_entry_t entry);
void vhpt_invalidate_all(void);
void vhpt_invalidate_asid(asid_t asid);


#endif


 /** @}
 */

