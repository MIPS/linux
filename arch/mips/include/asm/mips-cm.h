/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_MIPS_CM_H__
#define __MIPS_ASM_MIPS_CM_H__

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/types.h>

/* The base address of the CM GCR block */
extern void __iomem *mips_cm_base;

/* The base address of the CM L2-only sync region */
extern void __iomem *mips_cm_l2sync_base;

/**
 * __mips_cm_phys_base - retrieve the physical base address of the CM
 *
 * This function returns the physical base address of the Coherence Manager
 * global control block, or 0 if no Coherence Manager is present. It provides
 * a default implementation which reads the CMGCRBase register where available,
 * and may be overridden by platforms which determine this address in a
 * different way by defining a function with the same prototype except for the
 * name mips_cm_phys_base (without underscores).
 */
extern phys_addr_t __mips_cm_phys_base(void);

/*
 * mips_cm_is64 - determine CM register width
 *
 * The CM register width is determined by the version of the CM, with CM3
 * introducing 64 bit GCRs and all prior CM versions having 32 bit GCRs.
 * However we may run a kernel built for MIPS32 on a system with 64 bit GCRs,
 * or vice-versa. This variable indicates the width of the memory accesses
 * that the kernel will perform to GCRs, which may differ from the actual
 * width of the GCRs.
 *
 * It's set to 0 for 32-bit accesses and 1 for 64-bit accesses.
 */
extern int mips_cm_is64;

/**
 * mips_cm_error_report - Report CM cache errors
 */
#ifdef CONFIG_MIPS_CM
extern void mips_cm_error_report(void);
#else
static inline void mips_cm_error_report(void) {}
#endif

/**
 * mips_cm_probe - probe for a Coherence Manager
 *
 * Attempt to detect the presence of a Coherence Manager. Returns 0 if a CM
 * is successfully detected, else -errno.
 */
#ifdef CONFIG_MIPS_CM
extern int mips_cm_probe(void);
#else
static inline int mips_cm_probe(void)
{
	return -ENODEV;
}
#endif

/**
 * mips_cm_present - determine whether a Coherence Manager is present
 *
 * Returns true if a CM is present in the system, else false.
 */
static inline bool mips_cm_present(void)
{
#ifdef CONFIG_MIPS_CM
	return mips_cm_base != NULL;
#else
	return false;
#endif
}

/**
 * mips_cm_has_l2sync - determine whether an L2-only sync region is present
 *
 * Returns true if the system implements an L2-only sync region, else false.
 */
static inline bool mips_cm_has_l2sync(void)
{
#ifdef CONFIG_MIPS_CM
	return mips_cm_l2sync_base != NULL;
#else
	return false;
#endif
}

/* Offsets to register blocks from the CM base address */
#define MIPS_CM_GCB_OFS		0x0000 /* Global Control Block */
#define MIPS_CM_CLCB_OFS	0x2000 /* Core Local Control Block */
#define MIPS_CM_COCB_OFS	0x4000 /* Core Other Control Block */
#define MIPS_CM_GDB_OFS		0x6000 /* Global Debug Block */

/* Total size of the CM memory mapped registers */
#define MIPS_CM_GCR_SIZE	0x8000

/* Size of the L2-only sync region */
#define MIPS_CM_L2SYNC_SIZE	0x1000

/* Macros to ease the creation of register access functions */
#define BUILD_CM_R_(name, block, off, redir)					\
static inline unsigned long __iomem *addr##redir##_gcr_##name(void)		\
{										\
	return (unsigned long __iomem *)(mips_cm_base + (block) + (off));	\
}										\
										\
static inline u32 read32##redir##_gcr_##name(void)				\
{										\
	return __raw_readl(addr##redir##_gcr_##name());				\
}										\
										\
static inline u64 read64##redir##_gcr_##name(void)				\
{										\
	void __iomem *addr = addr##redir##_gcr_##name();			\
	u64 ret;								\
										\
	if (mips_cm_is64) {							\
		ret = __raw_readq(addr);					\
	} else {								\
		ret = __raw_readl(addr);					\
		ret |= (u64)__raw_readl(addr + 0x4) << 32;			\
	}									\
										\
	return ret;								\
}										\
										\
static inline unsigned long read##redir##_gcr_##name(void)			\
{										\
	if (mips_cm_is64)							\
		return read64##redir##_gcr_##name();				\
	else									\
		return read32##redir##_gcr_##name();				\
}

#define BUILD_CM__W(name, redir)						\
static inline void write32##redir##_gcr_##name(u32 value)			\
{										\
	__raw_writel(value, addr##redir##_gcr_##name());			\
}										\
										\
static inline void write64##redir##_gcr_##name(u64 value)			\
{										\
	__raw_writeq(value, addr##redir##_gcr_##name());			\
}										\
										\
static inline void write##redir##_gcr_##name(unsigned long value)		\
{										\
	if (mips_cm_is64)							\
		write64##redir##_gcr_##name(value);				\
	else									\
		write32##redir##_gcr_##name(value);				\
}

#define BUILD_GCR_R_(name, off)					\
	BUILD_CM_R_(name, MIPS_CM_GCB_OFS, off, )		\
	BUILD_CM_R_(name, MIPS_CM_COCB_OFS, off, _redir)

#define BUILD_GCR_RW(name, off)					\
	BUILD_GCR_R_(name, off)					\
	BUILD_CM__W(name, )					\
	BUILD_CM__W(name, _redir)

#define BUILD_CM_Cx_R_(name, off)				\
	BUILD_CM_R_(cl_##name, MIPS_CM_CLCB_OFS, (off), )	\
	BUILD_CM_R_(co_##name, MIPS_CM_COCB_OFS, (off), )

#define BUILD_CM_Cx__W(name)					\
	BUILD_CM__W(cl_##name, )				\
	BUILD_CM__W(co_##name, )

#define BUILD_CM_Cx_RW(name, off)				\
	BUILD_CM_Cx_R_(name, off)				\
	BUILD_CM_Cx__W(name)

/* GCB register accessor functions */
BUILD_GCR_R_(config,		0x00)
BUILD_GCR_RW(base,		0x08)
BUILD_GCR_RW(access,		0x20)
BUILD_GCR_R_(rev,		0x30)
BUILD_GCR_RW(err_control,	0x38)
BUILD_GCR_RW(error_mask,	0x40)
BUILD_GCR_RW(error_cause,	0x48)
BUILD_GCR_RW(error_addr,	0x50)
BUILD_GCR_RW(error_mult,	0x58)
BUILD_GCR_RW(l2_only_sync_base,	0x70)
BUILD_GCR_RW(gic_base,		0x80)
BUILD_GCR_RW(cpc_base,		0x88)
BUILD_GCR_RW(reg0_base,		0x90)
BUILD_GCR_RW(reg0_mask,		0x98)
BUILD_GCR_RW(reg1_base,		0xa0)
BUILD_GCR_RW(reg1_mask,		0xa8)
BUILD_GCR_RW(reg2_base,		0xb0)
BUILD_GCR_RW(reg2_mask,		0xb8)
BUILD_GCR_RW(reg3_base,		0xc0)
BUILD_GCR_RW(reg3_mask,		0xc8)
BUILD_GCR_R_(gic_status,	0xd0)
BUILD_GCR_R_(cpc_status,	0xf0)
BUILD_GCR_RW(l2_config,		0x130)
BUILD_GCR_RW(sys_config2,	0x150)
BUILD_GCR_RW(l2_pft_control,	0x300)
BUILD_GCR_RW(l2_pft_control_b,	0x308)
BUILD_GCR_RW(bev_base,		0x680)

/* Core Local & Core Other register accessor functions */
BUILD_CM_Cx_RW(reset_release,	0x00)
BUILD_CM_Cx_RW(coherence,	0x08)
BUILD_CM_Cx_R_(config,		0x10)
BUILD_CM_Cx_RW(other,		0x18)
BUILD_CM_Cx_RW(reset_base,	0x20)
BUILD_CM_Cx_R_(id,		0x28)
BUILD_CM_Cx_RW(reset_ext_base,	0x30)
BUILD_CM_Cx_R_(tcid_0_priority,	0x40)
BUILD_CM_Cx_R_(tcid_1_priority,	0x48)
BUILD_CM_Cx_R_(tcid_2_priority,	0x50)
BUILD_CM_Cx_R_(tcid_3_priority,	0x58)
BUILD_CM_Cx_R_(tcid_4_priority,	0x60)
BUILD_CM_Cx_R_(tcid_5_priority,	0x68)
BUILD_CM_Cx_R_(tcid_6_priority,	0x70)
BUILD_CM_Cx_R_(tcid_7_priority,	0x78)
BUILD_CM_Cx_R_(tcid_8_priority,	0x80)

/* GCR_CONFIG register fields */
#define CM3_GCR_CONFIG_NUMCLUSTERS_SHF		23
#define CM3_GCR_CONFIG_NUMCLUSTERS_MSK		(_ULCAST_(0x3f) << 23)
#define CM_GCR_CONFIG_NUMIOCU_SHF		8
#define CM_GCR_CONFIG_NUMIOCU_MSK		(_ULCAST_(0xf) << 8)
#define CM_GCR_CONFIG_PCORES_SHF		0
#define CM_GCR_CONFIG_PCORES_MSK		(_ULCAST_(0xff) << 0)

/* GCR_BASE register fields */
#define CM_GCR_BASE_GCRBASE_SHF			15
#define CM_GCR_BASE_GCRBASE_MSK			(_ULCAST_(0x1ffff) << 15)
#define CM_GCR_BASE_CMDEFTGT_SHF		0
#define CM_GCR_BASE_CMDEFTGT_MSK		(_ULCAST_(0x3) << 0)
#define  CM_GCR_BASE_CMDEFTGT_DISABLED		0
#define  CM_GCR_BASE_CMDEFTGT_MEM		1
#define  CM_GCR_BASE_CMDEFTGT_IOCU0		2
#define  CM_GCR_BASE_CMDEFTGT_IOCU1		3

/* GCR_RESET_EXT_BASE register fields */
#define CM_GCR_RESET_EXT_BASE_EVARESET		BIT(31)
#define CM_GCR_RESET_EXT_BASE_UEB		BIT(30)

/* GCR_ACCESS register fields */
#define CM_GCR_ACCESS_ACCESSEN_SHF		0
#define CM_GCR_ACCESS_ACCESSEN_MSK		(_ULCAST_(0xff) << 0)

/* GCR_REV register fields */
#define CM_GCR_REV_MAJOR_SHF			8
#define CM_GCR_REV_MAJOR_MSK			(_ULCAST_(0xff) << 8)
#define CM_GCR_REV_MINOR_SHF			0
#define CM_GCR_REV_MINOR_MSK			(_ULCAST_(0xff) << 0)

#define CM_ENCODE_REV(major, minor) \
		(((major) << CM_GCR_REV_MAJOR_SHF) | \
		 ((minor) << CM_GCR_REV_MINOR_SHF))

#define CM_REV_CM2				CM_ENCODE_REV(6, 0)
#define CM_REV_CM2_5				CM_ENCODE_REV(7, 0)
#define CM_REV_CM3				CM_ENCODE_REV(8, 0)
#define CM_REV_CM3_5				CM_ENCODE_REV(9, 0)

/* GCR_ERR_CONTROL register fields */
#define CM_GCR_ERR_CONTROL_L2_ECC_EN_SHF	1
#define CM_GCR_ERR_CONTROL_L2_ECC_EN_MSK	(_ULCAST_(0x1) << 1)
#define CM_GCR_ERR_CONTROL_L2_ECC_SUPPORT_SHF	0
#define CM_GCR_ERR_CONTROL_L2_ECC_SUPPORT_MSK	(_ULCAST_(0x1) << 0)

/* GCR_ERROR_CAUSE register fields */
#define CM_GCR_ERROR_CAUSE_ERRTYPE_SHF		27
#define CM_GCR_ERROR_CAUSE_ERRTYPE_MSK		(_ULCAST_(0x1f) << 27)
#define CM3_GCR_ERROR_CAUSE_ERRTYPE_SHF		58
#define CM3_GCR_ERROR_CAUSE_ERRTYPE_MSK		GENMASK_ULL(63, 58)
#define CM_GCR_ERROR_CAUSE_ERRINFO_SHF		0
#define CM_GCR_ERROR_CAUSE_ERRINGO_MSK		(_ULCAST_(0x7ffffff) << 0)

/* GCR_ERROR_MULT register fields */
#define CM_GCR_ERROR_MULT_ERR2ND_SHF		0
#define CM_GCR_ERROR_MULT_ERR2ND_MSK		(_ULCAST_(0x1f) << 0)

/* GCR_L2_ONLY_SYNC_BASE register fields */
#define CM_GCR_L2_ONLY_SYNC_BASE_SYNCBASE_SHF	12
#define CM_GCR_L2_ONLY_SYNC_BASE_SYNCBASE_MSK	(_ULCAST_(0xfffff) << 12)
#define CM_GCR_L2_ONLY_SYNC_BASE_SYNCEN_SHF	0
#define CM_GCR_L2_ONLY_SYNC_BASE_SYNCEN_MSK	(_ULCAST_(0x1) << 0)

/* GCR_GIC_BASE register fields */
#define CM_GCR_GIC_BASE_GICBASE_SHF		17
#define CM_GCR_GIC_BASE_GICBASE_MSK		(_ULCAST_(0x7fff) << 17)
#define CM_GCR_GIC_BASE_GICEN_SHF		0
#define CM_GCR_GIC_BASE_GICEN_MSK		(_ULCAST_(0x1) << 0)

/* GCR_CPC_BASE register fields */
#define CM_GCR_CPC_BASE_CPCBASE_SHF		15
#define CM_GCR_CPC_BASE_CPCBASE_MSK		(_ULCAST_(0x1ffff) << 15)
#define CM_GCR_CPC_BASE_CPCEN_SHF		0
#define CM_GCR_CPC_BASE_CPCEN_MSK		(_ULCAST_(0x1) << 0)

/* GCR_GIC_STATUS register fields */
#define CM_GCR_GIC_STATUS_GICEX_SHF		0
#define CM_GCR_GIC_STATUS_GICEX_MSK		(_ULCAST_(0x1) << 0)

/* GCR_REGn_BASE register fields */
#define CM_GCR_REGn_BASE_BASEADDR_SHF		16
#define CM_GCR_REGn_BASE_BASEADDR_MSK		(_ULCAST_(0xffff) << 16)

/* GCR_REGn_MASK register fields */
#define CM_GCR_REGn_MASK_ADDRMASK_SHF		16
#define CM_GCR_REGn_MASK_ADDRMASK_MSK		(_ULCAST_(0xffff) << 16)
#define CM_GCR_REGn_MASK_CCAOVR_SHF		5
#define CM_GCR_REGn_MASK_CCAOVR_MSK		(_ULCAST_(0x3) << 5)
#define CM_GCR_REGn_MASK_CCAOVREN_SHF		4
#define CM_GCR_REGn_MASK_CCAOVREN_MSK		(_ULCAST_(0x1) << 4)
#define CM_GCR_REGn_MASK_DROPL2_SHF		2
#define CM_GCR_REGn_MASK_DROPL2_MSK		(_ULCAST_(0x1) << 2)
#define CM_GCR_REGn_MASK_CMTGT_SHF		0
#define CM_GCR_REGn_MASK_CMTGT_MSK		(_ULCAST_(0x3) << 0)
#define  CM_GCR_REGn_MASK_CMTGT_DISABLED	(_ULCAST_(0x0) << 0)
#define  CM_GCR_REGn_MASK_CMTGT_MEM		(_ULCAST_(0x1) << 0)
#define  CM_GCR_REGn_MASK_CMTGT_IOCU0		(_ULCAST_(0x2) << 0)
#define  CM_GCR_REGn_MASK_CMTGT_IOCU1		(_ULCAST_(0x3) << 0)

/* GCR_GIC_STATUS register fields */
#define CM_GCR_GIC_STATUS_EX_SHF		0
#define CM_GCR_GIC_STATUS_EX_MSK		(_ULCAST_(0x1) << 0)

/* GCR_CPC_STATUS register fields */
#define CM_GCR_CPC_STATUS_EX_SHF		0
#define CM_GCR_CPC_STATUS_EX_MSK		(_ULCAST_(0x1) << 0)

/* GCR_L2_CONFIG register fields */
#define CM_GCR_L2_CONFIG_BYPASS_SHF		20
#define CM_GCR_L2_CONFIG_BYPASS_MSK		(_ULCAST_(0x1) << 20)
#define CM_GCR_L2_CONFIG_SET_SIZE_SHF		12
#define CM_GCR_L2_CONFIG_SET_SIZE_MSK		(_ULCAST_(0xf) << 12)
#define CM_GCR_L2_CONFIG_LINE_SIZE_SHF		8
#define CM_GCR_L2_CONFIG_LINE_SIZE_MSK		(_ULCAST_(0xf) << 8)
#define CM_GCR_L2_CONFIG_ASSOC_SHF		0
#define CM_GCR_L2_CONFIG_ASSOC_MSK		(_ULCAST_(0xff) << 0)

/* GCR_SYS_CONFIG2 register fields */
#define CM_GCR_SYS_CONFIG2_MAXVPW_SHF		0
#define CM_GCR_SYS_CONFIG2_MAXVPW_MSK		(_ULCAST_(0xf) << 0)

/* GCR_L2_PFT_CONTROL register fields */
#define CM_GCR_L2_PFT_CONTROL_PAGEMASK_SHF	12
#define CM_GCR_L2_PFT_CONTROL_PAGEMASK_MSK	(_ULCAST_(0xfffff) << 12)
#define CM_GCR_L2_PFT_CONTROL_PFTEN_SHF		8
#define CM_GCR_L2_PFT_CONTROL_PFTEN_MSK		(_ULCAST_(0x1) << 8)
#define CM_GCR_L2_PFT_CONTROL_NPFT_SHF		0
#define CM_GCR_L2_PFT_CONTROL_NPFT_MSK		(_ULCAST_(0xff) << 0)

/* GCR_L2_PFT_CONTROL_B register fields */
#define CM_GCR_L2_PFT_CONTROL_B_CEN_SHF		8
#define CM_GCR_L2_PFT_CONTROL_B_CEN_MSK		(_ULCAST_(0x1) << 8)
#define CM_GCR_L2_PFT_CONTROL_B_PORTID_SHF	0
#define CM_GCR_L2_PFT_CONTROL_B_PORTID_MSK	(_ULCAST_(0xff) << 0)

/* GCR_L2SM_COP register fields */
#define CM_GCR_L2SM_COP_PRESENT			BIT(31)
#define CM_GCR_L2SM_COP_RESULT_SHF		6
#define CM_GCR_L2SM_COP_RESULT_MSK		(_ULCAST_(0x7) << 6)
#define CM_GCR_L2SM_COP_RESULT_DONE_NOERR	(_ULCAST_(0x1) << 6)
#define CM_GCR_L2SM_COP_RUNNING			BIT(5)
#define CM_GCR_L2SM_COP_TYPE_SHF		2
#define CM_GCR_L2SM_COP_TYPE_MSK		(_ULCAST_(0x7) << 2)
#define CM_GCR_L2SM_COP_TYPE_STORE_TAG		(_ULCAST_(0x1) << 2)
#define CM_GCR_L2SM_COP_CMD_MSK			(_ULCAST_(0x3) << 0)
#define CM_GCR_L2SM_COP_CMD_START		(_ULCAST_(0x1) << 0)

/* GCR_L2SM_TAG_ADDR_COP register fields */
#define CM_GCR_L2SM_TAG_ADDR_COP_NUM_SHF	48
#define CM_GCR_L2SM_TAG_ADDR_COP_NUM_MSK	(_ULCAST_(0xffff) << 48)
#define CM_GCR_L2SM_TAG_ADDR_COP_START_SHF	6
#define CM_GCR_L2SM_TAG_ADDR_COP_START_MSK	(_ULCAST_(0x3ffffffffff) << 6)

/* GCR_Cx_COHERENCE register fields */
#define CM_GCR_Cx_COHERENCE_COHDOMAINEN_SHF	0
#define CM_GCR_Cx_COHERENCE_COHDOMAINEN_MSK	(_ULCAST_(0xff) << 0)
#define CM3_GCR_Cx_COHERENCE_COHEN_MSK		(_ULCAST_(0x1) << 0)

/* GCR_Cx_CONFIG register fields */
#define CM_GCR_Cx_CONFIG_IOCUTYPE_SHF		10
#define CM_GCR_Cx_CONFIG_IOCUTYPE_MSK		(_ULCAST_(0x3) << 10)
#define CM_GCR_Cx_CONFIG_PVPE_SHF		0
#define CM_GCR_Cx_CONFIG_PVPE_MSK		(_ULCAST_(0x3ff) << 0)

/* GCR_Cx_OTHER register fields */
#define CM_GCR_Cx_OTHER_CORENUM_SHF		16
#define CM_GCR_Cx_OTHER_CORENUM_MSK		(_ULCAST_(0xffff) << 16)
#define CM3_GCR_Cx_REDIRECT_CLUSTER_REDIREN_SHF	31
#define CM3_GCR_Cx_REDIRECT_CLUSTER_REDIREN_MSK	(_ULCAST_(0x1) << 31)
#define CM3_GCR_Cx_REDIRECT_GIC_REDIREN_SHF	30
#define CM3_GCR_Cx_REDIRECT_GIC_REDIREN_MSK	(_ULCAST_(0x1) << 30)
#define CM3_GCR_Cx_REDIRECT_BLOCK_SHF		24
#define CM3_GCR_Cx_REDIRECT_BLOCK_MSK		(_ULCAST_(0x3) << 24)
#define CM3_GCR_Cx_REDIRECT_CLUSTER_SHF		16
#define CM3_GCR_Cx_REDIRECT_CLUSTER_MSK		(_ULCAST_(0x3f) << 16)
#define CM3_GCR_Cx_OTHER_CORE_SHF		8
#define CM3_GCR_Cx_OTHER_CORE_MSK		(_ULCAST_(0x3f) << 8)
#define CM3_GCR_Cx_OTHER_VP_SHF			0
#define CM3_GCR_Cx_OTHER_VP_MSK			(_ULCAST_(0x7) << 0)

/* GCR_Cx_RESET_BASE register fields */
#define CM_GCR_Cx_RESET_BASE_BEVEXCBASE_SHF	12
#define CM_GCR_Cx_RESET_BASE_BEVEXCBASE_MSK	(_ULCAST_(0xfffff) << 12)

/* GCR_Cx_RESET_EXT_BASE register fields */
#define CM_GCR_Cx_RESET_EXT_BASE_EVARESET_SHF	31
#define CM_GCR_Cx_RESET_EXT_BASE_EVARESET_MSK	(_ULCAST_(0x1) << 31)
#define CM_GCR_Cx_RESET_EXT_BASE_UEB_SHF	30
#define CM_GCR_Cx_RESET_EXT_BASE_UEB_MSK	(_ULCAST_(0x1) << 30)
#define CM_GCR_Cx_RESET_EXT_BASE_BEVEXCMASK_SHF	20
#define CM_GCR_Cx_RESET_EXT_BASE_BEVEXCMASK_MSK	(_ULCAST_(0xff) << 20)
#define CM_GCR_Cx_RESET_EXT_BASE_BEVEXCPA_SHF	1
#define CM_GCR_Cx_RESET_EXT_BASE_BEVEXCPA_MSK	(_ULCAST_(0x7f) << 1)
#define CM_GCR_Cx_RESET_EXT_BASE_PRESENT_SHF	0
#define CM_GCR_Cx_RESET_EXT_BASE_PRESENT_MSK	(_ULCAST_(0x1) << 0)

/*
 * enum gcr_redir_block - blocks to target using GCR_Cx_REDIRECT
 *
 * Register blocks that a core or VP "other" register block can be redirected
 * to using the GCR_Cx_REDIRECT register, typically via mips_cm_lock_other().
 */
enum gcr_redir_block {
	/* CM GCR redirect blocks */
	BLOCK_GCR_CORE_LOCAL = 0,
	BLOCK_GCR_GLOBAL = 1,
	BLOCK_GCR_DEBUG = 2,

	/* CPC redirect blocks */
	BLOCK_CPC_CORE_LOCAL = 0,
	BLOCK_CPC_GLOBAL = 1,

	/* GIC redirect blocks */
	BLOCK_GIC_VP_LOCAL = 0,
	BLOCK_GIC_SHARED_LOWER = 1,
	BLOCK_GIC_USER = 2,
	BLOCK_GIC_SHARED_UPPER = 3,
};

#ifdef CONFIG_MIPS_CM

/**
 * mips_cm_lock_other - lock access to redirect region
 * @cluster: the other cluster to be accessed
 * @core: the other core to be accessed
 * @vp: the VP within the other core to be accessed
 * @block: the register block to be accessed
 *
 * Call in order to configure the redirect region to point at the register
 * block @block corresponding to the provided @cluster, @core & @vp numbers.
 * Must be followed by a call to mips_cm_unlock_other.
 */
extern void mips_cm_lock_other(unsigned int cluster, unsigned int core,
			       unsigned int vp, enum gcr_redir_block block);

/**
 * mips_cm_unlock_other - unlock access to another core
 *
 * Call after operating upon another core via the 'other' register region.
 * Must be called after mips_cm_lock_other.
 */
extern void mips_cm_unlock_other(void);

#else /* !CONFIG_MIPS_CM */

static inline void
mips_cm_lock_other(unsigned int cluster, unsigned int core,
		   unsigned int vp, enum gcr_redir_block block) { }
static inline void mips_cm_unlock_other(void) { }

#endif /* !CONFIG_MIPS_CM */

/**
 * mips_cm_lock_other_cpu - lock access to redirect region
 * @cpu: the CPU whose registers will be accessed
 * @block: the register block to be accessed
 *
 * Call in order to configure the redirect region to point at the register
 * block @block corresponding to the CPU @cpu. Must be followed by a call to
 * mips_cm_unlock_other.
 */
static inline void mips_cm_lock_other_cpu(unsigned int cpu,
					  enum gcr_redir_block block)
{
	mips_cm_lock_other(cpu_cluster(&cpu_data[cpu]),
			   cpu_core(&cpu_data[cpu]),
			   cpu_vpe_id(&cpu_data[cpu]),
			   block);
}

/*
 * mips_cm_numcores - return the number of cores present in the system
 *
 * Returns the value of the PCORES field of the GCR_CONFIG register plus 1, or
 * zero if no Coherence Manager is present.
 */
static inline unsigned mips_cm_numcores(void)
{
	if (!mips_cm_present())
		return 0;

	return ((read_gcr_config() & CM_GCR_CONFIG_PCORES_MSK)
		>> CM_GCR_CONFIG_PCORES_SHF) + 1;
}

/**
 * mips_cm_numiocu - return the number of IOCUs present in the system
 *
 * Returns the value of the NUMIOCU field of the GCR_CONFIG register, or zero
 * if no Coherence Manager is present.
 */
static inline unsigned mips_cm_numiocu(void)
{
	if (!mips_cm_present())
		return 0;

	return (read_gcr_config() & CM_GCR_CONFIG_NUMIOCU_MSK)
		>> CM_GCR_CONFIG_NUMIOCU_SHF;
}

/**
 * mips_cm_l2sync - perform an L2-only sync operation
 *
 * If an L2-only sync region is present in the system then this function
 * performs and L2-only sync and returns zero. Otherwise it returns -ENODEV.
 */
static inline int mips_cm_l2sync(void)
{
	if (!mips_cm_has_l2sync())
		return -ENODEV;

	writel(0, mips_cm_l2sync_base);
	return 0;
}

/**
 * mips_cm_revision() - return CM revision
 *
 * Return: The revision of the CM, from GCR_REV, or 0 if no CM is present. The
 * return value should be checked against the CM_REV_* macros.
 */
static inline int mips_cm_revision(void)
{
	if (!mips_cm_present())
		return 0;

	return read_gcr_rev();
}

/**
 * mips_cm_max_vp_width() - return the width in bits of VP indices
 *
 * Return: the width, in bits, of VP indices in fields that combine core & VP
 * indices.
 */
static inline unsigned int mips_cm_max_vp_width(void)
{
	extern int smp_num_siblings;
	uint32_t cfg;

	if (mips_cm_revision() >= CM_REV_CM3)
		return read_gcr_sys_config2() & CM_GCR_SYS_CONFIG2_MAXVPW_MSK;

	if (mips_cm_present()) {
		/*
		 * We presume that all cores in the system will have the same
		 * number of VP(E)s, and if that ever changes then this will
		 * need revisiting.
		 */
		cfg = read_gcr_cl_config() & CM_GCR_Cx_CONFIG_PVPE_MSK;
		return (cfg >> CM_GCR_Cx_CONFIG_PVPE_SHF) + 1;
	}

	if (IS_ENABLED(CONFIG_SMP))
		return smp_num_siblings;

	return 1;
}

/**
 * mips_cm_vp_id() - calculate the hardware VP ID for a CPU
 * @cpu: the CPU whose VP ID to calculate
 *
 * Hardware such as the GIC uses identifiers for VPs which may not match the
 * CPU numbers used by Linux. This function calculates the hardware VP
 * identifier corresponding to a given CPU.
 *
 * Return: the VP ID for the CPU.
 */
static inline unsigned int mips_cm_vp_id(unsigned int cpu)
{
	unsigned int core = cpu_core(&cpu_data[cpu]);
	unsigned int vp = cpu_vpe_id(&cpu_data[cpu]);

	return (core * mips_cm_max_vp_width()) + vp;
}

/**
 * mips_cm_numclusters() - return the number of clusters present in the system
 *
 * Returns the value of the NUM_CLUSTERS field of the GCR_CONFIG register where
 * implemented, or 1 if the system doesn't support clusters or no Coherence
 * Manager is present.
 */
static inline unsigned int mips_cm_numclusters(void)
{
	unsigned int cfg;

	if (mips_cm_revision() < CM_REV_CM3_5)
		return 1;

	cfg = read_gcr_config();
	cfg &= CM3_GCR_CONFIG_NUMCLUSTERS_MSK;
	cfg >>= CM3_GCR_CONFIG_NUMCLUSTERS_SHF;

	return cfg;
}

/**
 * mips_cm_using_multicluster() - determine whether multiple clusters are in use
 *
 * Returns true if the system is using multiple clusters, otherwise false. This
 * is useful for callers that can act more optimally if they know whether they
 * need to act upon multiple clusters or not.
 */
static inline bool mips_cm_using_multicluster(void)
{
	unsigned int last_cpu;

	/*
	 * We rely upon CPUs being probed in each cluster in order, with CPUs
	 * in secondary clusters coming after the boot cluster (cluster 0). This
	 * means that we can determine whether multiple clusters are in use purely
	 * by examining whether the last possible CPU is in the boot cluster.
	 */
	last_cpu = find_last_bit(cpumask_bits(cpu_possible_mask), nr_cpumask_bits);
	return cpu_cluster(&cpu_data[last_cpu]) != 0;
}

/**
 * __mips_cm_first_cluster() - Find the first cluster number from a cpumask
 * @cpumask: mask containing CPUs whose clusters we want to cover
 *
 * Find the cluster number for the first CPU set in @cpumask. Not intended for
 * direct use - instead make use of this via the for_each_possible_cluster()
 * macro.
 *
 * Return: the cluster number of the first CPU set in @cpumask
 */
static inline unsigned int
__mips_cm_first_cluster(const struct cpumask *cpumask)
{
	return cpu_cluster(&cpu_data[cpumask_first(cpumask)]);
}

/**
 * __mips_cm_next_cluster() - Find the next cluster covering a cpumask
 * @cpumask: mask containing CPUs whose clusters we want to cover
 * @prev: the cluster to start from
 *
 * Find the cluster number for the cluster following @prev which contains CPUs
 * set within @cpumask. Not intended for direct use - instead make use of this
 * via the for_each_possible_cluster() macro.
 *
 * Return: the cluster number following @prev, or UINT_MAX if no more clusters
 */
static inline unsigned int
__mips_cm_next_cluster(const struct cpumask *cpumask, unsigned int prev)
{
	unsigned int cpu;

	/*
	 * We rely here upon having probed CPUs from each cluster sequentially
	 * with a strictly incrementing cluster number. That is, each CPU
	 * should have a cluster number greater or equal than that of all CPUs
	 * with a lower CPU number.
	 */
	for_each_cpu(cpu, cpumask) {
		if (cpu_cluster(&cpu_data[cpu]) <= prev)
			continue;

		return cpu_cluster(&cpu_data[cpu]);
	}

	return UINT_MAX;
}

/*
 * for_each_possible_cluster() - Loop over clusters containing possible CPUs
 * @cluster: an unsigned integer to contain the cluster number
 *
 * Loop over all clusters which contain any CPUs set in cpu_possible_mask. This
 * can be used to easily operate on all clusters that Linux is running across.
 * For example you may access a register in all clusters by doing something
 * along the lines of:
 *
 *   unsigned int cluster;
 *   for_each_possible_cluster(cluster) {
 *     mips_cm_lock_other(cluster, 0, 0, BLOCK_GCR_GLOBAL);
 *     write_redir_gcr_gic_base(0x10000001);
 *     mips_cm_unlock_other();
 *   }
 */
#define for_each_possible_cluster(cluster)					\
	for ((cluster) = __mips_cm_first_cluster(cpu_possible_mask);		\
	     (cluster) != UINT_MAX;						\
	     (cluster) = __mips_cm_next_cluster(cpu_possible_mask, cluster))

#endif /* __MIPS_ASM_MIPS_CM_H__ */
