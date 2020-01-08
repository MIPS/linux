/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01, 02 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 * Copyright (C) 2017 Imagination Technologies Ltd.
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/asm-offsets.h>
#include <asm/sysmips.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/war.h>

	.align	5
NESTED(handle_sys, PT_SIZE, sp)
	.set	noat
	SAVE_SOME
	TRACE_IRQS_ON_RELOAD
	STI
	.set	at

	# There is a SYSCALL[16] encoding too, but we only support SYSCALL[32]
	PTR_L	t1, PT_EPC(sp)		# skip syscall on return
	PTR_ADDIU t1, 4			# skip to next instruction
	PTR_S	t1, PT_EPC(sp)

	LONG_S	v0, PT_R26(sp)		# save v0/a0 for syscall restarting

	li	t1, _TIF_WORK_SYSCALL_ENTRY
	LONG_L	t0, TI_FLAGS($28)	# syscall tracing enabled?
	and	t0, t1, t0
	bnez	t0, syscall_trace_entry

syscall_common:
	# Syscall number is in t4 ($2)
	sltiu   t0, t4, __NR_syscalls
	beqz	t0, illegal_syscall

	PTR_LA	t0, sys_call_table
	PTR_LXS	t0, t4(t0)		# load entry from table

	jalr	t0			# Do The Real Thing (TM)

	li	t0, 1			# trigger syscall restart check
	LONG_S	t0, PT_R0(sp)
	LONG_S	v0, PT_R4(sp)		# result

syscall_exit:
	j	syscall_exit_partial

/* ------------------------------------------------------------------------ */

syscall_trace_entry:
	SAVE_STATIC
	move	a0, sp
	move	a1, t4
	jal	syscall_trace_enter

	bltz	v0, 1f			# seccomp failed? Skip syscall

	RESTORE_STATIC
	LONG_L	t4, PT_R2(sp)		# Restore syscall (maybe modified)
	LONG_L	a0, PT_R4(sp)		# Restore argument registers
	LONG_L	a1, PT_R5(sp)
	LONG_L	a2, PT_R6(sp)
	LONG_L	a3, PT_R7(sp)
	LONG_L	a4, PT_R8(sp)
	LONG_L	a5, PT_R9(sp)
	j	syscall_common

1:	j	syscall_exit

illegal_syscall:
	/* This also isn't a valid syscall, throw an error.  */
	li	v0, -ENOSYS			# error
	LONG_S	v0, PT_R4(sp)
	j	syscall_exit
	END(handle_sys)
