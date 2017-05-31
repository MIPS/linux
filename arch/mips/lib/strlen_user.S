/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1998, 1999, 2004 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2011 MIPS Technologies, Inc.
 */
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/export.h>
#include <asm/regdef.h>

#define EX(insn,reg,addr,handler)			\
9:	insn	reg, addr;				\
	.section __ex_table,"a";			\
	PTR	9b, handler;				\
	.previous

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */
	.macro __BUILD_STRLEN_ASM func
LEAF(__strlen_\func\()_asm)
	LONG_L		t0, TI_ADDR_LIMIT($28)	# pointer ok?
	and		t0, a0
	bnez		t0, .Lfault\@

	move		t0, a0
.ifeqs "\func", "kernel"
1:	EX(lbu, t1, (t0), .Lfault\@)
.else
1:	EX(lbue, t1, (t0), .Lfault\@)
.endif
	PTR_ADDIU	t0, 1
	bnez		t1, 1b
	PTR_SUBU	t0, a0
	jr		ra
	END(__strlen_\func\()_asm)

.Lfault\@:	move		v0, zero
	jr		ra
	.endm

#ifndef CONFIG_EVA
	/* Set aliases */
	.global __strlen_user_asm
	.set __strlen_user_asm, __strlen_kernel_asm
EXPORT_SYMBOL(__strlen_user_asm)
#endif

__BUILD_STRLEN_ASM kernel
EXPORT_SYMBOL(__strlen_kernel_asm)

#ifdef CONFIG_EVA

	.set push
	.set eva
__BUILD_STRLEN_ASM user
	.set pop
EXPORT_SYMBOL(__strlen_user_asm)
#endif
