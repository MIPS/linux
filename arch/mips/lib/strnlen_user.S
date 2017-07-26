/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1996, 1998, 1999, 2004 by Ralf Baechle
 * Copyright (c) 1999 Silicon Graphics, Inc.
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
 * Return the size of a string including the ending NUL character up to a
 * maximum of a1 or 0 in case of error.
 *
 * Note: for performance reasons we deliberately accept that a user may
 *	 make strlen_user and strnlen_user access the first few KSEG0
 *	 bytes.	 There's nothing secret there.	On 64-bit accessing beyond
 *	 the maximum is a tad hairier ...
 */
	.macro __BUILD_STRNLEN_ASM func
LEAF(__strnlen_\func\()_asm)
	LONG_L		t0, TI_ADDR_LIMIT($28)	# pointer ok?
	and		t0, a0
	bnez		t0, .Lfault\@

	move		t0, a0
	PTR_ADDU	t1, a1, a0			# stop pointer
	beq		t0, t1, 2f		# limit reached?
1:
#ifdef CONFIG_CPU_DADDI_WORKAROUNDS
	.set		noat
	li		AT, 1
#endif
.ifeqs "\func", "kernel"
	EX(lb, t2, (t0), .Lfault\@)
.else
	EX(lbe, t2, (t0), .Lfault\@)
.endif
#ifndef CONFIG_CPU_DADDI_WORKAROUNDS
	PTR_ADDIU	t0, 1
#else
	PTR_ADDU	t0, AT
	.set		at
#endif
	beq		t0, t1, 2f		# limit reached?
	bnez		t2, 1b
2:	PTR_SUBU	v0, t0, a0
	jr		ra
	END(__strnlen_\func\()_asm)

.Lfault\@:
	move		v0, zero
	jr		ra
	.endm

#ifndef CONFIG_EVA
	/* Set aliases */
	.global __strnlen_user_asm
	.set __strnlen_user_asm, __strnlen_kernel_asm
EXPORT_SYMBOL(__strnlen_user_asm)
#endif

__BUILD_STRNLEN_ASM kernel
EXPORT_SYMBOL(__strnlen_kernel_asm)

#ifdef CONFIG_EVA

	.set push
	.set eva
__BUILD_STRNLEN_ASM user
	.set pop
EXPORT_SYMBOL(__strnlen_user_asm)
#endif
