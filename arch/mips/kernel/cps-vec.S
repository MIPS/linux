/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/asmmacro.h>
#include <asm/cacheops.h>
#include <asm/eva.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/pm.h>
#include <asm/thread_info.h>

#define GCR_CPC_BASE_OFS	0x0088
#define GCR_CL_COHERENCE_OFS	0x2008
#define GCR_CL_ID_OFS		0x2028
#define  CM3_GCR_Cx_ID_CLUSTER_SHF	8
#define  CM3_GCR_Cx_ID_CLUSTER_MSK	(0xff << 8)
#define  CM3_GCR_Cx_ID_CORENUM_SHF	0
#define  CM3_GCR_Cx_ID_CORENUM_MSK	(0xff << 0)

#define CPC_CL_CMD_OFS		0x2000
#define CPC_CL_CMD_PWRDOWN	0x2

#define CPC_CL_VC_STOP_OFS	0x2020
#define CPC_CL_VC_RUN_OFS	0x2028

.extern mips_cm_base

#ifdef CONFIG_64BIT
# define STATUS_BITDEPS		ST0_KX
#else
# define STATUS_BITDEPS		0
#endif

#ifdef CONFIG_MIPS_CPS_NS16550

#define DUMP_EXCEP(name)		\
	PTR_LA	a0, 8f;			\
	jal	mips_cps_bev_dump;	\
	TEXT(name)

#else /* !CONFIG_MIPS_CPS_NS16550 */

#define DUMP_EXCEP(name)

#endif /* !CONFIG_MIPS_CPS_NS16550 */

	/*
	 * Set dest to non-zero if the core supports the MT ASE, else zero. If
	 * MT is not supported then branch to nomt.
	 */
	.macro	has_mt	dest, nomt
	mfc0	\dest, CP0_CONFIG, 1
	bgez	\dest, \nomt
	mfc0	\dest, CP0_CONFIG, 2
	bgez	\dest, \nomt
	mfc0	\dest, CP0_CONFIG, 3
	andi	\dest, \dest, MIPS_CONF3_MT
	beqz	\dest, \nomt
	.endm

	/*
	 * Set dest to non-zero if the core supports MIPSr6 multithreading
	 * (ie. VPs), else zero. If MIPSr6 multithreading is not supported then
	 * branch to nomt.
	 */
	.macro	has_vp	dest, nomt
	mfc0	\dest, CP0_CONFIG, 1
	bgez	\dest, \nomt
	mfc0	\dest, CP0_CONFIG, 2
	bgez	\dest, \nomt
	mfc0	\dest, CP0_CONFIG, 3
	bgez	\dest, \nomt
	mfc0	\dest, CP0_CONFIG, 4
	bgez	\dest, \nomt
	mfc0	\dest, CP0_CONFIG, 5
	andi	\dest, \dest, MIPS_CONF5_VP
	beqz	\dest, \nomt
	.endm

	/* Calculate an uncached address for the CM GCRs */
	.macro	cmgcrb	dest
	.set	push
	.set	noat
	MFC0	$1, CP0_CMGCRBASE
	PTR_SLL	$1, $1, 4
	PTR_LI	\dest, UNCAC_BASE
	PTR_ADDU \dest, \dest, $1
	.set	pop
	.endm

.section .text.cps-vec
.balign 0x1000

LEAF(mips_cps_core_entry)
	/*
	 * These first 4 bytes will be patched by cps_smp_setup to load the
	 * CCA to use into register s0.
	 */
	.word	0

	/* Check whether we're here due to an NMI */
	mfc0	k0, CP0_STATUS
	and	k0, k0, ST0_NMI
	beqz	k0, not_nmi

	/* This is an NMI */
	PTR_LA	k0, nmi_handler
	jr	k0

not_nmi:
	/* Setup Cause */
	li	t0, CAUSEF_IV
	mtc0	t0, CP0_CAUSE

	/* Setup Status */
	li	t0, ST0_CU1 | ST0_CU0 | ST0_BEV | STATUS_BITDEPS
	mtc0	t0, CP0_STATUS

	/* Skip cache & coherence setup if we're already coherent */
	cmgcrb	t8
	lw	s7, GCR_CL_COHERENCE_OFS(t8)
	bnez	s7, 1f

	/* Initialize the L1 caches */
	jal	mips_cps_cache_init

	/* Enter the coherent domain */
	li	t0, 0xff
	sw	t0, GCR_CL_COHERENCE_OFS(t8)
	ehb

	/* Set Kseg0 CCA to that in s0 */
1:	mfc0	t0, CP0_CONFIG
	ori	t0, 0x7
	xori	t0, 0x7
	or	t0, t0, s0
	mtc0	t0, CP0_CONFIG
	ehb

	/* Jump to kseg0 */
	PTR_LA	t0, 1f
	jr	t0

	/*
	 * We're up, cached & coherent. Perform any EVA initialization necessary
	 * before we access memory.
	 */
1:	eva_init

	/* Retrieve boot configuration pointers */
	jal	mips_cps_get_bootcfg

	/* Skip core-level init if we started up coherent */
	bnez	s7, 1f

	/* Perform any further required core-level initialisation */
	jal	mips_cps_core_init

	/*
	 * Boot any other VPEs within this core that should be online, and
	 * deactivate this VPE if it should be offline.
	 */
	move	a0, v0
	move	a1, t9
	jal	mips_cps_boot_vpes

	/* Off we go! */
1:	PTR_L	t1, VPEBOOTCFG_PC(s1)
	PTR_L	gp, VPEBOOTCFG_GP(s1)
	PTR_L	sp, VPEBOOTCFG_SP(s1)
	jr	t1
	END(mips_cps_core_entry)

.org 0x200
LEAF(excep_tlbfill)
	DUMP_EXCEP("TLB Fill")
	b	.
	END(excep_tlbfill)

.org 0x280
LEAF(excep_xtlbfill)
	DUMP_EXCEP("XTLB Fill")
	b	.
	END(excep_xtlbfill)

.org 0x300
LEAF(excep_cache)
	DUMP_EXCEP("Cache")
	b	.
	END(excep_cache)

.org 0x380
LEAF(excep_genex)
	DUMP_EXCEP("General")
	b	.
	END(excep_genex)

.org 0x400
LEAF(excep_intex)
	DUMP_EXCEP("Interrupt")
	b	.
	END(excep_intex)

.org 0x480
LEAF(excep_ejtag)
	PTR_LA	k0, ejtag_debug_handler
	jr	k0
	END(excep_ejtag)

LEAF(mips_cps_core_init)
#ifdef CONFIG_MIPS_MT_SMP
	/* Check that the core implements the MT ASE */
	has_mt	t0, 3f

	.set	push
	.set	MIPS_ISA_LEVEL_RAW
	.set	mt

	/* Only allow 1 TC per VPE to execute... */
	dmt

	/* ...and for the moment only 1 VPE */
	dvpe
	PTR_LA	t1, 1f
	jr.hb	t1

	/* Enter VPE configuration state */
1:	mfc0	t0, CP0_MVPCONTROL
	ori	t0, t0, MVPCONTROL_VPC
	mtc0	t0, CP0_MVPCONTROL

	/* Retrieve the number of VPEs within the core */
	mfc0	t0, CP0_MVPCONF0
	srl	t0, t0, MVPCONF0_PVPE_SHIFT
	andi	t0, t0, (MVPCONF0_PVPE >> MVPCONF0_PVPE_SHIFT)
	addiu	ta3, t0, 1

	/* If there's only 1, we're done */
	beqz	t0, 2f

	/* Loop through each VPE within this core */
	li	ta1, 1

1:	/* Operate on the appropriate TC */
	mtc0	ta1, CP0_VPECONTROL
	ehb

	/* Bind TC to VPE (1:1 TC:VPE mapping) */
	mttc0	ta1, CP0_TCBIND

	/* Set exclusive TC, non-active, master */
	li	t0, VPECONF0_MVP
	sll	t1, ta1, VPECONF0_XTC_SHIFT
	or	t0, t0, t1
	mttc0	t0, CP0_VPECONF0

	/* Set TC non-active, non-allocatable */
	mttc0	zero, CP0_TCSTATUS

	/* Set TC halted */
	li	t0, TCHALT_H
	mttc0	t0, CP0_TCHALT

	/* Next VPE */
	addiu	ta1, ta1, 1
	slt	t0, ta1, ta3
	bnez	t0, 1b

	/* Leave VPE configuration state */
2:	mfc0	t0, CP0_MVPCONTROL
	xori	t0, t0, MVPCONTROL_VPC
	mtc0	t0, CP0_MVPCONTROL

3:	.set	pop
#endif
	jr	ra
	END(mips_cps_core_init)

/**
 * mips_cps_get_bootcfg() - retrieve boot configuration pointers
 *
 * Returns: pointer to struct core_boot_config in v0, pointer to
 *          struct vpe_boot_config in s1, VPE ID in t9
 */
LEAF(mips_cps_get_bootcfg)
	/* Calculate a pointer to this cores struct core_boot_config */
	PTR_LA	v0, mips_cps_cluster_bootcfg
	PTR_L	v0, 0(v0)
	cmgcrb	t0
	lw	t0, GCR_CL_ID_OFS(t0)
#ifdef CONFIG_CPU_MIPSR6
	ext	t1, t0, CM3_GCR_Cx_ID_CLUSTER_SHF, 8
	li	t2, CLUSTERBOOTCFG_SIZE
	mul	t1, t1, t2
	PTR_ADDU \
		v0, v0, t1
#endif
	PTR_L	v0, CLUSTERBOOTCFG_CORECONFIG(v0)
	andi	t0, t0, CM3_GCR_Cx_ID_CORENUM_MSK
	li	t1, COREBOOTCFG_SIZE
	mul	t0, t0, t1
	PTR_ADDU v0, v0, t0

	/* Calculate this VPEs ID. If the core doesn't support MT use 0 */
	li	t9, 0
#if defined(CONFIG_CPU_MIPSR6)
	has_vp	ta2, 1f

	/*
	 * Assume non-contiguous numbering. Perhaps some day we'll need
	 * to handle contiguous VP numbering, but no such systems yet
	 * exist.
	 */
	mfc0	t9, CP0_GLOBALNUMBER
	andi	t9, t9, MIPS_GLOBALNUMBER_VP
#endif
1:
#if defined(CONFIG_MIPS_MT_SMP)
	has_mt	ta2, 1f

	/* Find the number of VPEs present in the core */
	mfc0	t1, CP0_MVPCONF0
	srl	t1, t1, MVPCONF0_PVPE_SHIFT
	andi	t1, t1, MVPCONF0_PVPE >> MVPCONF0_PVPE_SHIFT
	addiu	t1, t1, 1

	/* Calculate a mask for the VPE ID from EBase.CPUNum */
	clz	t1, t1
	li	t2, 31
	subu	t1, t2, t1
	li	t2, 1
	sll	t1, t2, t1
	addiu	t1, t1, -1

	/* Retrieve the VPE ID from EBase.CPUNum */
	mfc0	t9, $15, 1
	and	t9, t9, t1
#endif

1:	/* Calculate a pointer to this VPEs struct vpe_boot_config */
	li	t1, VPEBOOTCFG_SIZE
	mul	s1, t9, t1
	PTR_L	ta3, COREBOOTCFG_VPECONFIG(v0)
	PTR_ADDU s1, s1, ta3

	jr	ra
	END(mips_cps_get_bootcfg)

LEAF(mips_cps_boot_vpes)
	lw	ta2, COREBOOTCFG_VPEMASK(a0)
	PTR_L	ta3, COREBOOTCFG_VPECONFIG(a0)

#if defined(CONFIG_CPU_MIPSR6)

	has_vp	t0, 5f

	/* Find base address of CPC */
	cmgcrb	t3
	PTR_L	t1, GCR_CPC_BASE_OFS(t3)
	PTR_LI	t2, ~0x7fff
	and	t1, t1, t2
	PTR_LI	t2, UNCAC_BASE
	PTR_ADD	t1, t1, t2

	/* Start any other VPs that ought to be running */
	PTR_S	ta2, CPC_CL_VC_RUN_OFS(t1)

	/* Ensure this VP stops running if it shouldn't be */
	not	ta2
	PTR_S	ta2, CPC_CL_VC_STOP_OFS(t1)
	ehb

#endif

#if defined(CONFIG_MIPS_MT)

	/* If the core doesn't support MT then return */
5:	has_mt	t0, 5f

	/* Enter VPE configuration state */
	.set	push
	.set	MIPS_ISA_LEVEL_RAW
	.set	mt
	dvpe
	.set	pop

	PTR_LA	t1, 1f
	jr.hb	t1
1:	mfc0	t1, CP0_MVPCONTROL
	ori	t1, t1, MVPCONTROL_VPC
	mtc0	t1, CP0_MVPCONTROL
	ehb

	/* Loop through each VPE */
	move	t8, ta2
	li	ta1, 0

	/* Check whether the VPE should be running. If not, skip it */
1:	andi	t0, ta2, 1
	beqz	t0, 2f

	/* Operate on the appropriate TC */
	mfc0	t0, CP0_VPECONTROL
	ori	t0, t0, VPECONTROL_TARGTC
	xori	t0, t0, VPECONTROL_TARGTC
	or	t0, t0, ta1
	mtc0	t0, CP0_VPECONTROL
	ehb

	.set	push
	.set	MIPS_ISA_LEVEL_RAW
	.set	mt

	/* Skip the VPE if its TC is not halted */
	mftc0	t0, CP0_TCHALT
	beqz	t0, 2f

	/* Calculate a pointer to the VPEs struct vpe_boot_config */
	li	t0, VPEBOOTCFG_SIZE
	mul	t0, t0, ta1
	addu	t0, t0, ta3

	/* Set the TC restart PC */
	lw	t1, VPEBOOTCFG_PC(t0)
	mttc0	t1, CP0_TCRESTART

	/* Set the TC stack pointer */
	lw	t1, VPEBOOTCFG_SP(t0)
	mttgpr	t1, sp
	mttgpr	sp, t1

	/* Set the TC global pointer */
	lw	t1, VPEBOOTCFG_GP(t0)
	mttgpr	t1, gp
	mttgpr	gp, t1

	/* Copy config from this VPE */
	mfc0	t0, CP0_CONFIG
	mttc0	t0, CP0_CONFIG

	/*
	 * Copy the EVA config from this VPE if the CPU supports it.
	 * CONFIG3 must exist to be running MT startup - just read it.
	 */
	mfc0	t0, CP0_CONFIG, 3
	and	t0, t0, MIPS_CONF3_SC
	beqz	t0, 3f
	mfc0    t0, CP0_SEGCTL0
	mttc0	t0, CP0_SEGCTL0
	mfc0    t0, CP0_SEGCTL1
	mttc0	t0, CP0_SEGCTL1
	mfc0    t0, CP0_SEGCTL2
	mttc0	t0, CP0_SEGCTL2
3:
	/* Ensure no software interrupts are pending */
	mttc0	zero, CP0_CAUSE
	mttc0	zero, CP0_STATUS

	/* Set TC active, not interrupt exempt */
	mftc0	t0, CP0_TCSTATUS
	li	t1, ~TCSTATUS_IXMT
	and	t0, t0, t1
	or	t0, t0, TCSTATUS_A
	mttc0	t0, CP0_TCSTATUS

	/* Clear the TC halt bit */
	mttc0	zero, CP0_TCHALT

	/* Set VPE active */
	mftc0	t0, CP0_VPECONF0
	ori	t0, t0, VPECONF0_VPA
	mttc0	t0, CP0_VPECONF0

	/* Next VPE */
2:	srl	ta2, ta2, 1
	addiu	ta1, ta1, 1
	bnez	ta2, 1b

	/* Leave VPE configuration state */
	mfc0	t1, CP0_MVPCONTROL
	xori	t1, t1, MVPCONTROL_VPC
	mtc0	t1, CP0_MVPCONTROL
	ehb
	evpe

	.set	pop

	/* Check whether this VPE is meant to be running */
	li	t0, 1
	sll	t0, t0, a1
	and	t0, t0, t8
	bnez	t0, 2f

	/* This VPE should be offline, halt the TC */
	li	t0, TCHALT_H
	mtc0	t0, CP0_TCHALT
	PTR_LA	t0, 1f
1:	jr.hb	t0

2:

#endif /* CONFIG_MIPS_MT_SMP */

	/* Return */
5:	jr	ra
	END(mips_cps_boot_vpes)

LEAF(mips_cps_cache_init)
	/*
	 * Clear the bits used to index the caches. Note that the architecture
	 * dictates that writing to any of TagLo or TagHi selects 0 or 2 should
	 * be valid for all MIPS32 CPUs, even those for which said writes are
	 * unnecessary.
	 */
	mtc0	zero, CP0_TAGLO, 0
	mtc0	zero, CP0_TAGHI, 0
	mtc0	zero, CP0_TAGLO, 2
	mtc0	zero, CP0_TAGHI, 2
	ehb

	/* Primary cache configuration is indicated by Config1 */
	mfc0	ta0, CP0_CONFIG, 1

	/* Detect I-cache line size */
	_EXT	t0, ta0, MIPS_CONF1_IL_SHF, MIPS_CONF1_IL_SZ
	li	t1, 2
	beqz	t0, icache_done
	sllv	t0, t1, t0

	/* Detect I-cache size */
	_EXT	t1, ta0, MIPS_CONF1_IS_SHF, MIPS_CONF1_IS_SZ
	xori	t2, t1, 0x7
	li	t3, 32
	beqz	t2, 1f
	addiu	t1, t1, 1
	sllv	t1, t3, t1
1:	/* At this point t1 == I-cache sets per way */
	_EXT	t2, ta0, MIPS_CONF1_IA_SHF, MIPS_CONF1_IA_SZ
	addiu	t2, t2, 1
	mul	t1, t1, t0
	mul	t1, t1, t2

	li	a0, CKSEG0
	PTR_ADD	a1, a0, t1
1:	cache	Index_Store_Tag_I, 0(a0)
	PTR_ADD	a0, a0, t0
	bne	a0, a1, 1b
icache_done:

	/* Detect D-cache line size */
	_EXT	t0, ta0, MIPS_CONF1_DL_SHF, MIPS_CONF1_DL_SZ
	li	t1, 2
	beqz	t0, dcache_done
	sllv	t0, t1, t0

	/* Detect D-cache size */
	_EXT	t1, ta0, MIPS_CONF1_DS_SHF, MIPS_CONF1_DS_SZ
	xori	t2, t1, 0x7
	li	t3, 32
	beqz	t2, 1f
	addiu	t1, t1, 1
	sllv	t1, t3, t1
1:	/* At this point t1 == D-cache sets per way */
	_EXT	t2, ta0, MIPS_CONF1_DA_SHF, MIPS_CONF1_DA_SZ
	addiu	t2, t2, 1
	mul	t1, t1, t0
	mul	t1, t1, t2

	li	a0, CKSEG0
	PTR_ADDU a1, a0, t1
1:	cache	Index_Store_Tag_D, 0(a0)
	PTR_ADD a0, a0, t0
	bne	a0, a1, 1b
dcache_done:

	jr	ra
	END(mips_cps_cache_init)

#if defined(CONFIG_MIPS_CPS_PM) && defined(CONFIG_CPU_PM)

	/* Calculate a pointer to this CPUs struct mips_static_suspend_state */
	.macro	psstate	dest
	.set	push
	.set	noat
	ASM_CPUID_MFC0	$1, ASM_SMP_CPUID_REG
	LONG_SRL	$1, SMP_CPUID_PTRSHIFT
	PTR_LA	\dest, __per_cpu_offset
	addu	$1, $1, \dest
	lw	$1, 0($1)
	PTR_LA	\dest, cps_cpu_state
	addu	\dest, \dest, $1
	.set	pop
	.endm

LEAF(mips_cps_pm_save)
	/* Save CPU state */
	SUSPEND_SAVE_REGS
	psstate	t1
	SUSPEND_SAVE_STATIC
	jr	v0
	END(mips_cps_pm_save)

LEAF(mips_cps_pm_restore)
	/* Restore CPU state */
	psstate	t1
	RESUME_RESTORE_STATIC
	RESUME_RESTORE_REGS_RETURN
	END(mips_cps_pm_restore)

#endif /* CONFIG_MIPS_CPS_PM && CONFIG_CPU_PM */

#ifdef CONFIG_CPU_NANOMIPS
/*
 * Hardcode PM entry / exit code for Shaolin
 * until uasm grows support for nanomips.
 */
LEAF(nanomips_cps_nc_entry_fn_2)
	/* Enter state 2 (power gated) */
	cmgcrb	t8

	/* if (coupled_coherence) { == 1 */
	sync
lbl_incready:
	ll	t1, 0(a1)
	addiu	t2, t1, 1
	sc	t2, 0(a1)
	beqz	t2, lbl_incready
	 addiu	t1, t1, 1
	sync

	/* If last VPE, go to disable coherence */
	beq	t1, a0, lbl_disable_coherence
	 nop

	/* Halt this TC to stop VPE */
	addiu	t0, zero, TCHALT_H
	mtc0	t0, CP0_TCHALT

lbl_secondary_hang:
	b	lbl_secondary_hang
	 nop


lbl_disable_coherence:
	/* Invalidate L1 icache */
	mfc0	ta0, CP0_CONFIG, 1

	/* Detect I-cache line size */
	_EXT	t0, ta0, MIPS_CONF1_IL_SHF, MIPS_CONF1_IL_SZ
	li	t1, 2
	beqz	t0, icache_invalidated
	 sllv	t0, t1, t0

	/* Detect I-cache size */
	_EXT	t1, ta0, MIPS_CONF1_IS_SHF, MIPS_CONF1_IS_SZ
	xori	t2, t1, 0x7
	li	t3, 32
	beqz	t2, 1f
	 addiu	t1, t1, 1
	sllv	t1, t3, t1
1:	/* At this point t1 == I-cache sets per way */
	_EXT	t2, ta0, MIPS_CONF1_IA_SHF, MIPS_CONF1_IA_SZ
	addiu	t2, t2, 1
	mul	t1, t1, t0
	mul	t1, t1, t2

	li	a0, CKSEG0
	PTR_ADD	a1, a0, t1
1:	cache	Index_Invalidate_I, 0(a0)
	PTR_ADD	a0, a0, t0
	bne	a0, a1, 1b
	 nop

icache_invalidated:
	/* Invalidate L1 dcache */

	/* Detect D-cache line size */
	_EXT	t0, ta0, MIPS_CONF1_DL_SHF, MIPS_CONF1_DL_SZ
	li	t1, 2
	beqz	t0, dcache_writtenback
	 sllv	t0, t1, t0

	/* Detect D-cache size */
	_EXT	t1, ta0, MIPS_CONF1_DS_SHF, MIPS_CONF1_DS_SZ
	xori	t2, t1, 0x7
	li	t3, 32
	beqz	t2, 1f
	 addiu	t1, t1, 1
	sllv	t1, t3, t1
1:	/* At this point t1 == D-cache sets per way */
	_EXT	t2, ta0, MIPS_CONF1_DA_SHF, MIPS_CONF1_DA_SZ
	addiu	t2, t2, 1
	mul	t1, t1, t0
	mul	t1, t1, t2

	li	a0, CKSEG0
	PTR_ADDU a1, a0, t1
1:	cache	Index_Writeback_Inv_D, 0(a0)
	PTR_ADD a0, a0, t0
	bne	a0, a1, 1b
	 nop

dcache_writtenback:
	/* Barrier ensuring previous cache invalidates are complete */
	sync
	ehb

	/* Disable all but self interventions */
	PTR_L	t1, GCR_CL_ID_OFS(t8)
	_EXT	t1, t1, CM3_GCR_Cx_ID_CORENUM_SHF, 8
	LI	t2, 1
	sllv	t1, t2, t1
	PTR_S	t1, GCR_CL_COHERENCE_OFS(t8)
	PTR_L	t1, GCR_CL_COHERENCE_OFS(t8)

	/* Barrier to ensure write to coherence control is complete */
	sync
	ehb

	/* Disable coherence */
	PTR_S	zero, GCR_CL_COHERENCE_OFS(t8)
	PTR_L	t1, GCR_CL_COHERENCE_OFS(t8)

	/* Find base address of CPC */
	PTR_L	t1, GCR_CPC_BASE_OFS(t8)
	PTR_LI	t2, ~0x7fff
	and	t1, t1, t2
	PTR_LI	t2, UNCAC_BASE
	PTR_ADD	t1, t1, t2

	/* Powerdown this core via CPC */
	addiu	t2, zero, CPC_CL_CMD_PWRDOWN
	PTR_S	t2, CPC_CL_CMD_OFS(t1)

lbl_hang:
	b	lbl_hang
	 nop

	END(nanomips_cps_nc_entry_fn_2)

#endif /* CONFIG_CPU_NANOMIPS */
