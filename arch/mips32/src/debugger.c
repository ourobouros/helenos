/*
 * Copyright (C) 2005 Ondrej Palkovsky
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

 /** @addtogroup mips32debug mips32
 * @ingroup debug
 * @{
 */
/** @file
 */

#include <arch/debugger.h>
#include <memstr.h>
#include <console/kconsole.h>
#include <console/cmd.h>
#include <symtab.h>
#include <print.h>
#include <panic.h>
#include <arch.h>
#include <arch/cp0.h>
#include <func.h>

bpinfo_t breakpoints[BKPOINTS_MAX];
SPINLOCK_INITIALIZE(bkpoint_lock);

static int cmd_print_breakpoints(cmd_arg_t *argv);
static cmd_info_t bkpts_info = {
	.name = "bkpts",
	.description = "Print breakpoint table.",
	.func = cmd_print_breakpoints,
	.argc = 0,
};

static int cmd_del_breakpoint(cmd_arg_t *argv);
static cmd_arg_t del_argv = {
	.type = ARG_TYPE_INT
};
static cmd_info_t delbkpt_info = {
	.name = "delbkpt",
	.description = "delbkpt <number> - Delete breakpoint.",
	.func = cmd_del_breakpoint,
	.argc = 1,
	.argv = &del_argv
};

static int cmd_add_breakpoint(cmd_arg_t *argv);
static cmd_arg_t add_argv = {
	.type = ARG_TYPE_INT
};
static cmd_info_t addbkpt_info = {
	.name = "addbkpt",
	.description = "addbkpt <&symbol> - new bkpoint. Break on J/Branch insts unsupported.",
	.func = cmd_add_breakpoint,
	.argc = 1,
	.argv = &add_argv
};

static cmd_arg_t adde_argv[] = {
	{ .type = ARG_TYPE_INT },
	{ .type = ARG_TYPE_INT }
};
static cmd_info_t addbkpte_info = {
	.name = "addbkpte",
	.description = "addebkpte <&symbol> <&func> - new bkpoint. Call func(or Nothing if 0).",
	.func = cmd_add_breakpoint,
	.argc = 2,
	.argv = adde_argv
};

static struct {
	__u32 andmask;
	__u32 value;
}jmpinstr[] = {
	{0xf3ff0000, 0x41000000}, /* BCzF */
	{0xf3ff0000, 0x41020000}, /* BCzFL */
	{0xf3ff0000, 0x41010000}, /* BCzT */
	{0xf3ff0000, 0x41030000}, /* BCzTL */
	{0xfc000000, 0x10000000}, /* BEQ */
	{0xfc000000, 0x50000000}, /* BEQL */
	{0xfc1f0000, 0x04010000}, /* BEQL */
	{0xfc1f0000, 0x04110000}, /* BGEZAL */
	{0xfc1f0000, 0x04130000}, /* BGEZALL */
	{0xfc1f0000, 0x04030000}, /* BGEZL */
	{0xfc1f0000, 0x1c000000}, /* BGTZ */
	{0xfc1f0000, 0x5c000000}, /* BGTZL */
	{0xfc1f0000, 0x18000000}, /* BLEZ */
	{0xfc1f0000, 0x58000000}, /* BLEZL */
	{0xfc1f0000, 0x04000000}, /* BLTZ */
	{0xfc1f0000, 0x04100000}, /* BLTZAL */
	{0xfc1f0000, 0x04120000}, /* BLTZALL */
	{0xfc1f0000, 0x04020000}, /* BLTZL */
	{0xfc000000, 0x14000000}, /* BNE */
	{0xfc000000, 0x54000000}, /* BNEL */
	{0xfc000000, 0x08000000}, /* J */
	{0xfc000000, 0x0c000000}, /* JAL */
	{0xfc1f07ff, 0x00000009}, /* JALR */
	{0,0} /* EndOfTable */
};

/** Test, if the given instruction is a jump or branch instruction
 *
 * @param instr Instruction code
 * @return true - it is jump instruction, false otherwise
 */
static bool is_jump(__native instr)
{
	int i;

	for (i=0;jmpinstr[i].andmask;i++) {
		if ((instr & jmpinstr[i].andmask) == jmpinstr[i].value)
			return true;
	}

	return false;
}

/** Add new breakpoint to table */
int cmd_add_breakpoint(cmd_arg_t *argv)
{
	bpinfo_t *cur = NULL;
	ipl_t ipl;
	int i;

	if (argv->intval & 0x3) {
		printf("Not aligned instruction, forgot to use &symbol?\n");
		return 1;
	}
	ipl = interrupts_disable();
	spinlock_lock(&bkpoint_lock);

	/* Check, that the breakpoints do not conflict */
	for (i=0; i<BKPOINTS_MAX; i++) {
		if (breakpoints[i].address == (__address)argv->intval) {
			printf("Duplicate breakpoint %d.\n", i);
			spinlock_unlock(&bkpoints_lock);
			return 0;
		} else if (breakpoints[i].address == (__address)argv->intval + sizeof(__native) || \
			   breakpoints[i].address == (__address)argv->intval - sizeof(__native)) {
			printf("Adjacent breakpoints not supported, conflict with %d.\n", i);
			spinlock_unlock(&bkpoints_lock);
			return 0;
		}
			
	}

	for (i=0; i<BKPOINTS_MAX; i++)
		if (!breakpoints[i].address) {
			cur = &breakpoints[i];
			break;
		}
	if (!cur) {
		printf("Too many breakpoints.\n");
		spinlock_unlock(&bkpoint_lock);
		interrupts_restore(ipl);
		return 0;
	}
	cur->address = (__address) argv->intval;
	printf("Adding breakpoint on address: %p\n", argv->intval);
	cur->instruction = ((__native *)cur->address)[0];
	cur->nextinstruction = ((__native *)cur->address)[1];
	if (argv == &add_argv) {
		cur->flags = 0;
	} else { /* We are add extended */
		cur->flags = BKPOINT_FUNCCALL;
		cur->bkfunc = 	(void (*)(void *, istate_t *)) argv[1].intval;
	}
	if (is_jump(cur->instruction))
		cur->flags |= BKPOINT_ONESHOT;
	cur->counter = 0;

	/* Set breakpoint */
	*((__native *)cur->address) = 0x0d;

	spinlock_unlock(&bkpoint_lock);
	interrupts_restore(ipl);

	return 1;
}



/** Remove breakpoint from table */
int cmd_del_breakpoint(cmd_arg_t *argv)
{
	bpinfo_t *cur;
	ipl_t ipl;

	if (argv->intval < 0 || argv->intval > BKPOINTS_MAX) {
		printf("Invalid breakpoint number.\n");
		return 0;
	}
	ipl = interrupts_disable();
	spinlock_lock(&bkpoint_lock);

	cur = &breakpoints[argv->intval];
	if (!cur->address) {
		printf("Breakpoint does not exist.\n");
		spinlock_unlock(&bkpoint_lock);
		interrupts_restore(ipl);
		return 0;
	}
	if ((cur->flags & BKPOINT_INPROG) && (cur->flags & BKPOINT_ONESHOT)) {
		printf("Cannot remove one-shot breakpoint in-progress\n");
		spinlock_unlock(&bkpoint_lock);
		interrupts_restore(ipl);
		return 0;
	}
	((__u32 *)cur->address)[0] = cur->instruction;
	((__u32 *)cur->address)[1] = cur->nextinstruction;

	cur->address = NULL;

	spinlock_unlock(&bkpoint_lock);
	interrupts_restore(ipl);
	return 1;
}

/** Print table of active breakpoints */
int cmd_print_breakpoints(cmd_arg_t *argv)
{
	int i;
	char *symbol;

	printf("Breakpoint table.\n");
	for (i=0; i < BKPOINTS_MAX; i++)
		if (breakpoints[i].address) {
			symbol = get_symtab_entry(breakpoints[i].address);
			printf("%d. %p in %s\n",i,
			       breakpoints[i].address, symbol);
			printf("     Count(%d) ", breakpoints[i].counter);
			if (breakpoints[i].flags & BKPOINT_INPROG)
				printf("INPROG ");
			if (breakpoints[i].flags & BKPOINT_ONESHOT)
				printf("ONESHOT ");
			if (breakpoints[i].flags & BKPOINT_FUNCCALL)
				printf("FUNCCALL ");
			printf("\n");
		}
	return 1;
}

/** Initialize debugger */
void debugger_init()
{
	int i;

	for (i=0; i<BKPOINTS_MAX; i++)
		breakpoints[i].address = NULL;
	
	cmd_initialize(&bkpts_info);
	if (!cmd_register(&bkpts_info))
		panic("could not register command %s\n", bkpts_info.name);

	cmd_initialize(&delbkpt_info);
	if (!cmd_register(&delbkpt_info))
		panic("could not register command %s\n", delbkpt_info.name);

	cmd_initialize(&addbkpt_info);
	if (!cmd_register(&addbkpt_info))
		panic("could not register command %s\n", addbkpt_info.name);

	cmd_initialize(&addbkpte_info);
	if (!cmd_register(&addbkpte_info))
		panic("could not register command %s\n", addbkpte_info.name);
}

/** Handle breakpoint
 *
 * Find breakpoint in breakpoint table. 
 * If found, call kconsole, set break on next instruction and reexecute.
 * If we are on "next instruction", set it back on the first and reexecute.
 * If breakpoint not found in breakpoint table, call kconsole and start
 * next instruction.
 */
void debugger_bpoint(istate_t *istate)
{
	bpinfo_t *cur = NULL;
	__address fireaddr = istate->epc;
	int i;

	/* test branch delay slot */
	if (cp0_cause_read() & 0x80000000)
		panic("Breakpoint in branch delay slot not supported.\n");

	spinlock_lock(&bkpoint_lock);
	for (i=0; i<BKPOINTS_MAX; i++) {
		/* Normal breakpoint */
		if (fireaddr == breakpoints[i].address \
		    && !(breakpoints[i].flags & BKPOINT_REINST)) {
			cur = &breakpoints[i];
			break;
		}
		/* Reinst only breakpoint */
		if ((breakpoints[i].flags & BKPOINT_REINST) \
		    && (fireaddr ==breakpoints[i].address+sizeof(__native))) {
			cur = &breakpoints[i];
			break;
		}
	}
	if (cur) {
		if (cur->flags & BKPOINT_REINST) {
			/* Set breakpoint on first instruction */
			((__u32 *)cur->address)[0] = 0x0d;
			/* Return back the second */
			((__u32 *)cur->address)[1] = cur->nextinstruction;
			cur->flags &= ~BKPOINT_REINST;
			spinlock_unlock(&bkpoint_lock);
			return;
		} 
		if (cur->flags & BKPOINT_INPROG)
			printf("Warning: breakpoint recursion\n");
		
		if (!(cur->flags & BKPOINT_FUNCCALL))
			printf("***Breakpoint %d: %p in %s.\n", i, 
			       fireaddr, get_symtab_entry(istate->epc));

		/* Return first instruction back */
		((__u32 *)cur->address)[0] = cur->instruction;

		if (! (cur->flags & BKPOINT_ONESHOT)) {
			/* Set Breakpoint on next instruction */
			((__u32 *)cur->address)[1] = 0x0d;
			cur->flags |= BKPOINT_REINST;
		} 
		cur->flags |= BKPOINT_INPROG;
	} else {
		printf("***Breakpoint %p in %s.\n", fireaddr, 
		       get_symtab_entry(fireaddr));
		/* Move on to next instruction */
		istate->epc += 4;
	}
	if (cur)
		cur->counter++;
	if (cur && (cur->flags & BKPOINT_FUNCCALL)) {
		/* Allow zero bkfunc, just for counting */
		if (cur->bkfunc)
			cur->bkfunc(cur, istate);
	} else {
		printf("***Type 'exit' to exit kconsole.\n");
		/* This disables all other processors - we are not SMP,
		 * actually this gets us to cpu_halt, if scheduler() is run
		 * - we generally do not want scheduler to be run from debug,
		 *   so this is a good idea
		 */	
		atomic_set(&haltstate,1);
		spinlock_unlock(&bkpoint_lock);

		kconsole("debug");

		spinlock_lock(&bkpoint_lock);
		atomic_set(&haltstate,0);
	}
	if (cur && cur->address == fireaddr && (cur->flags & BKPOINT_INPROG)) {
		/* Remove one-shot breakpoint */
		if ((cur->flags & BKPOINT_ONESHOT))
			cur->address = NULL;
		/* Remove in-progress flag */
		cur->flags &= ~BKPOINT_INPROG;
	} 
	spinlock_unlock(&bkpoint_lock);
}

 /** @}
 */

