/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Micro-assembler generated tlb handler functions.
 *
 * Copyright (C) 2013  Broadcom Corporation.
 *
 * Based on mm/page-funcs.c
 * Copyright (C) 2012  MIPS Technologies, Inc.
 * Copyright (C) 2012  Ralf Baechle <ralf@linux-mips.org>
 */
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/thread_info.h>

#define FASTPATH_SIZE	128

.set	noat

#if defined(CONFIG_CPU_NANOMIPS)

#define CP0_KSCRATCH $31
#define KSCRATCH_PGD 2
#define KSCRATCH_TLBEX_TMP 3

EXPORT(tlbmiss_handler_setup_pgd_start)
LEAF(tlbmiss_handler_setup_pgd)
	.set	push
	.set	at
#ifndef CONFIG_MIPS_PGD_C0_CONTEXT
	la	t0, pgd_current
#ifdef CONFIG_SMP
	mfc0	t1, $SMP_CPUID_REG
	srl	t1, t1, SMP_CPUID_PTRSHIFT
	addu	t0, t0, t1
#endif
	sw	a0, 0(t0)
#endif
	mtc0	a0, CP0_KSCRATCH, KSCRATCH_PGD
	jr	ra
	.set	pop
END(tlbmiss_handler_setup_pgd)
EXPORT(tlbmiss_handler_setup_pgd_end)

.macro tlbchange_head
	mtc0	$1, CP0_KSCRATCH, KSCRATCH_TLBEX_TMP
	mfc0	k0, CP0_KSCRATCH, KSCRATCH_PGD
	mfc0	k1, CP0_BADVADDR
	srl	$1, k1, _PGDIR_SHIFT
	lsa	k0, $1, k0, _PGD_T_LOG2
	lw	k0, 0(k0)
#if 0
	srl	k1, k1, _PAGE_SHIFT - _PTE_T_LOG2
	andi	k1, k1, (_PTRS_PER_PTE - 1) << _PTE_T_LOG2
	addu	k0, k0, k1
#else
	ext	k1, k1, _PAGE_SHIFT, _PGDIR_SHIFT - _PAGE_SHIFT
	lsa	k0, k1, k0, _PTE_T_LOG2
#endif
#ifdef CONFIG_SMP
0:	ll	k1, 0(k0)
#else
	lw	k1, 0(k0)
#endif
	tlbp
.endm

.macro tlbchange_tail
#ifdef CONFIG_SMP
	sc	k1, 0(k0)
	beqz	k1, 0b
#else
	sw	k1, 0(k0)
#endif
	ori	k0, k0, _PTE_T_SIZE

	lw	$1, -_PTE_T_SIZE(k0)
	srl	$1, $1, __PAGE_NO_EXEC_SHIFT
	rotr	$1, $1, __PAGE_GLOBAL_SHIFT - __PAGE_NO_EXEC_SHIFT
	mtc0	$1, CP0_ENTRYLO0

	lw	k1, 0(k0)
	srl	k1, k1, __PAGE_NO_EXEC_SHIFT
	rotr	k1, k1, __PAGE_GLOBAL_SHIFT - __PAGE_NO_EXEC_SHIFT
	mtc0	k1, CP0_ENTRYLO1

	ehb
	tlbwi

	mfc0	$1, CP0_KSCRATCH, KSCRATCH_TLBEX_TMP
	eret
.endm

LEAF(handle_tlbm)
	tlbchange_head

	andi	$1, k1, __PAGE_WRITE
	beqz	$1, 1f

	ori	k1, k1, __PAGE_ACCESSED | __PAGE_MODIFIED | __PAGE_VALID | __PAGE_DIRTY

	tlbchange_tail
1:
	mfc0	$1, CP0_KSCRATCH, KSCRATCH_TLBEX_TMP
	j	tlb_do_page_fault_1
END(handle_tlbm)
EXPORT(handle_tlbm_end)

LEAF(handle_tlbs)
	tlbchange_head

	andi	$1, k1, __PAGE_PRESENT | __PAGE_WRITE
	xori	$1, $1, __PAGE_PRESENT | __PAGE_WRITE
	bnez	$1, 1f

	ori	k1, k1, __PAGE_ACCESSED | __PAGE_MODIFIED | __PAGE_VALID | __PAGE_DIRTY

	tlbchange_tail
1:
	mfc0	$1, CP0_KSCRATCH, KSCRATCH_TLBEX_TMP
	j	tlb_do_page_fault_1
END(handle_tlbs)
EXPORT(handle_tlbs_end)

LEAF(handle_tlbl)
	tlbchange_head

	andi	$1, k1, __PAGE_PRESENT
	beqz	$1, 1f

	ori	k1, k1, __PAGE_ACCESSED | __PAGE_VALID

	tlbchange_tail
1:
	mfc0	$1, CP0_KSCRATCH, KSCRATCH_TLBEX_TMP
	j	tlb_do_page_fault_0
END(handle_tlbl)
EXPORT(handle_tlbl_end)

LEAF(handle_tlb_refill)
	mfc0	k0, CP0_KSCRATCH, KSCRATCH_PGD

	mfc0	k1, CP0_BADVADDR
	srl	k1, k1, _PGDIR_SHIFT
	lsa	k0, k1, k0, _PGD_T_LOG2
	lw	k0, 0(k0)

	mfc0	k1, CP0_BADVADDR
	ext	k1, k1, _PAGE_SHIFT + 1, _PGDIR_SHIFT - _PAGE_SHIFT - 1
	lsa	k1, k1, k0, _PTE_T_LOG2 + 1
	lw	k0, 0(k1)
	lw	k1, _PTE_T_SIZE(k1)

	srl	k0, k0, __PAGE_NO_EXEC_SHIFT
	rotr	k0, k0, __PAGE_GLOBAL_SHIFT - __PAGE_NO_EXEC_SHIFT
	mtc0	k0, CP0_ENTRYLO0

	srl	k1, k1, __PAGE_NO_EXEC_SHIFT
	rotr	k1, k1, __PAGE_GLOBAL_SHIFT - __PAGE_NO_EXEC_SHIFT
	mtc0	k1, CP0_ENTRYLO1

	ehb
	tlbwr
	eret
END(handle_tlb_refill)

#else /* !CONFIG_TLBEX_STATIC */

EXPORT(tlbmiss_handler_setup_pgd_start)
LEAF(tlbmiss_handler_setup_pgd)
1:	j	1b		/* Dummy, will be replaced. */
	.space	64
END(tlbmiss_handler_setup_pgd)
EXPORT(tlbmiss_handler_setup_pgd_end)

LEAF(handle_tlbm)
	.space		FASTPATH_SIZE * 4
END(handle_tlbm)
EXPORT(handle_tlbm_end)

LEAF(handle_tlbs)
	.space		FASTPATH_SIZE * 4
END(handle_tlbs)
EXPORT(handle_tlbs_end)

LEAF(handle_tlbl)
	.space		FASTPATH_SIZE * 4
END(handle_tlbl)
EXPORT(handle_tlbl_end)

#endif /* !CONFIG_TLBEX_STATIC */
