/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) "malta: " fmt

#include <linux/errno.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/libfdt.h>
#include <linux/mc146818rtc.h>
#include <linux/of_fdt.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/printk.h>
#include <linux/sizes.h>

#include <asm/fw/fw.h>
#include <asm/machine.h>
#include <asm/mips-cm.h>
#include <asm/pci.h>
#include <asm/yamon-dt.h>

/* MIPS_REVISION register identifying the Malta & its configuration */
#define MIPS_REVISION				CKSEG1ADDR(0x1fc00010)
#define  MIPS_REVISION_MACHINE			(0xf << 4)
#define  MIPS_REVISION_MACHINE_MALTA		(0x2 << 4)
#define  MIPS_REVISION_CORID			(0x3f << 10)
#define  MIPS_REVISION_CORID_QED_RM5261		(0x0 << 10)
#define  MIPS_REVISION_CORID_CORE_LV		(0x1 << 10)
#define  MIPS_REVISION_CORID_BONITO64		(0x2 << 10)
#define  MIPS_REVISION_CORID_CORE_20K		(0x3 << 10)
#define  MIPS_REVISION_CORID_CORE_FPGA		(0x4 << 10)
#define  MIPS_REVISION_CORID_CORE_MSC		(0x5 << 10)
#define  MIPS_REVISION_CORID_CORE_EMUL		(0x6 << 10)
#define  MIPS_REVISION_CORID_CORE_FPGA2		(0x7 << 10)
#define  MIPS_REVISION_CORID_CORE_FPGAR2	(0x8 << 10)
#define  MIPS_REVISION_CORID_CORE_24K		(0xa << 10)
#define  MIPS_REVISION_SCON			(0xff << 24)
#define  MIPS_REVISION_SCON_OTHER		(0x0 << 24)
#define  MIPS_REVISION_SCON_SOCITSC		(0x1 << 24)
#define  MIPS_REVISION_SCON_SOCITSCP		(0x2 << 24)

/* System controllers without real MIPS_REVISION values */
#define MIPS_REVISION_SCON_GT64120		(-1)
#define MIPS_REVISION_SCON_BONITO		(-2)
#define MIPS_REVISION_SCON_SOCIT		(-3)
#define MIPS_REVISION_SCON_ROCIT		(-4)

/* Registers provided by the Galileo GT-64120 system controller */
#define GT64120_SIZE				0x2000
#define GT64120_PCI0_IO_LOW			0x48
#define GT64120_PCI0_IO_HIGH			0x50
#define GT64120_PCI0_M0_LOW			0x58
#define GT64120_PCI0_M0_HIGH			0x60
#define GT64120_PCI0_M1_LOW			0x80
#define GT64120_PCI0_M1_HIGH			0x88

/* Registers provided by the Bonito64 system controller */
#define BONITO_PCI_ID				CKSEG1ADDR(0x1fe00000)

/* Registers provided by the MSC01 family of system controllers */
#define MSC01_PCI_BASE				0x1bd00000
#define MSC01_PCI_BASE_SOCITSC			0x1ff10000
#define MSC01_PCI_SIZE				0x4000
#define MSC01_PCI_SC2PMBASL			0x0208
#define MSC01_PCI_SC2PMMSKL			0x0218
#define MSC01_PCI_SC2PMMAPL			0x0228
#define MSC01_PCI_SC2PIOBASL			0x0248
#define MSC01_PCI_SC2PIOMSKL			0x0258
#define MSC01_PCI_SC2PIOMAPL			0x0268
#define MSC01_PCI_P2SCMSKL			0x0308
#define MSC01_PCI_P2SCMAPL			0x0318
#define MSC01_PCI_HEAD4				0x2020
#define MSC01_PCI_BAR0				0x2220
#define MSC01_PCI_BAR0_SIZE			GENMASK(31, 4)
#define MSC01_BIU_SC_CFG			CKSEG1ADDR(0x1bc80110)
#define  MSC01_BIU_SC_CFG_GICPRES		BIT(2)
#define  MSC01_BIU_SC_CFG_GICENA		BIT(3)

#define ROCIT_CONFIG_GEN0			CKSEG1ADDR(0x1f403000)
#define  ROCIT_CONFIG_GEN0_PCI_IOCU		BIT(7)

#define ROCIT_CONFIG_GEN1			CKSEG1ADDR(0x1f403004)
#define  ROCIT_CONFIG_GEN1_MEMMAP_SHIFT		8
#define  ROCIT_CONFIG_GEN1_MEMMAP_MASK		(0xf << 8)


enum mem_map {
	MEM_MAP_V1 = 0,
	MEM_MAP_V2,
};

static int __initdata malta_core;
static int __initdata malta_syscon;
static bool __initdata malta_has_gic;

/*
 * Memory map V1
 *
 * We have a 32 bit physical memory map with a 2GB DDR region aliased in the
 * upper & lower halves of it. The I/O region obscures 256MB from
 * 0x10000000-0x1fffffff in the low alias but the DDR it obscures is accessible
 * via the high alias.
 *
 * Simply access everything beyond the lowest 256MB of DDR using the high alias.
 */
static struct yamon_mem_region __initdata malta_mem_regions_v1[] = {
	/* start		size */
	{ 0,			SZ_256M },
	{ SZ_2G + SZ_256M,	SZ_2G - SZ_256M },
	{}
};

/*
 * Memory map V2
 *
 * We have a flat 32 bit physical memory map with DDR filling all 4GB of the
 * memory map, apart from the I/O region which obscures 256MB from
 * 0x10000000-0x1fffffff.
 *
 * Therefore we discard the 256MB behind the I/O region.
 */
static struct yamon_mem_region __initdata malta_mem_regions_v2[] = {
	/* start	size			discard */
	{ 0,		SZ_256M,		SZ_256M },
	{ SZ_512M,	SZ_2G - SZ_512M + SZ_2G },
	{}
};

static __init bool malta_detect_bonito_pci(void)
{
	uint32_t pci_id, vendor, dev;

	pci_id = __raw_readl((void *)BONITO_PCI_ID);
	vendor = pci_id & 0xffff;
	dev = pci_id >> 16;

	if (vendor != PCI_VENDOR_ID_ALGORITHMICS)
		return false;

	switch (dev) {
	case PCI_DEVICE_ID_ALGORITHMICS_BONITO64_1:
	case PCI_DEVICE_ID_ALGORITHMICS_BONITO64_3:
		return true;

	default:
		return false;
	}
}

static __init bool malta_detect_gic(void)
{
	uint32_t sc_cfg;
	int err;

	/* If we have a CM, it will indicate GIC presence */
	err = mips_cm_probe();
	if (!err && (read_gcr_gic_status() & CM_GCR_GIC_STATUS_GICEX_MSK))
		return true;

	/*
	 * Some systems using the RocIT system controller feature a standalone
	 * GIC without a CM. Detect such systems below. We know that if the
	 * system controller is not RocIT then we're not dealing with such a
	 * system.
	 */
	if (malta_syscon != MIPS_REVISION_SCON_ROCIT)
		return false;

	/* Now check for the GICPres bit being set */
	sc_cfg = __raw_readl((void *)MSC01_BIU_SC_CFG);
	if (!(sc_cfg & MSC01_BIU_SC_CFG_GICPRES))
		return false;

	/* There is a standalone GIC, enable it */
	sc_cfg |= MSC01_BIU_SC_CFG_GICENA;
	__raw_writel(sc_cfg, (void *)MSC01_BIU_SC_CFG);

	return true;
}

static __init bool malta_detect(void)
{
	uint32_t rev;

	rev = __raw_readl((void *)MIPS_REVISION);

	if ((rev & MIPS_REVISION_MACHINE) != MIPS_REVISION_MACHINE_MALTA)
		return false;

	malta_core = rev & MIPS_REVISION_CORID;
	malta_syscon = rev & MIPS_REVISION_SCON;

	if (malta_syscon == MIPS_REVISION_SCON_OTHER) {
		/*
		 * The MIPS_REVISION register doesn't indicate the actual
		 * system controller in use, so we need to figure it out from
		 * the type of core card in use.
		 */
		switch (malta_core) {
		case MIPS_REVISION_CORID_CORE_EMUL:
			/*
			 * Emulator core cards may use either Bonito64 or RocIT
			 * system controllers. Detect which is in use by
			 * checking for Bonito64 PCI devices.
			 */
			if (malta_detect_bonito_pci())
				malta_syscon = MIPS_REVISION_SCON_BONITO;
			else
				malta_syscon = MIPS_REVISION_SCON_ROCIT;
			break;

		case MIPS_REVISION_CORID_QED_RM5261:
		case MIPS_REVISION_CORID_CORE_LV:
		case MIPS_REVISION_CORID_CORE_FPGA:
		case MIPS_REVISION_CORID_CORE_FPGAR2:
			malta_syscon = MIPS_REVISION_SCON_GT64120;
			break;

		case MIPS_REVISION_CORID_BONITO64:
		case MIPS_REVISION_CORID_CORE_20K:
			malta_syscon = MIPS_REVISION_SCON_BONITO;
			break;

		case MIPS_REVISION_CORID_CORE_MSC:
		case MIPS_REVISION_CORID_CORE_FPGA2:
		case MIPS_REVISION_CORID_CORE_24K:
			malta_syscon = MIPS_REVISION_SCON_SOCIT;
			break;

		default:
			malta_syscon = MIPS_REVISION_SCON_ROCIT;
			break;
		}
	}

	malta_has_gic = malta_detect_gic();

	return true;
}

static __init void malta_prom_init(void)
{
	PCIBIOS_MIN_IO = 0x1000;

	switch (malta_syscon) {
	case MIPS_REVISION_SCON_GT64120:
		set_io_port_base(CKSEG1ADDR(0x18000000));
		break;

	case MIPS_REVISION_SCON_BONITO:
		set_io_port_base(CKSEG1ADDR(0x1fd00000));
		break;

	case MIPS_REVISION_SCON_SOCIT:
	case MIPS_REVISION_SCON_SOCITSC:
	case MIPS_REVISION_SCON_SOCITSCP:
	case MIPS_REVISION_SCON_ROCIT:
		set_io_port_base(CKSEG1ADDR(0x1b000000));
		break;

	default:
		panic("Unhandled system controller");
	}
}

static __init int dt_append_memory(void *fdt)
{
	struct yamon_mem_region *mem_regions;
	enum mem_map mem_map;
	u32 config;

	/* detect the memory map in use */
	if (malta_syscon == MIPS_REVISION_SCON_ROCIT) {
		/* ROCit has a register indicating the memory map in use */
		config = readl((void __iomem *)CKSEG1ADDR(ROCIT_CONFIG_GEN1));
		mem_map = config & ROCIT_CONFIG_GEN1_MEMMAP_MASK;
		mem_map >>= ROCIT_CONFIG_GEN1_MEMMAP_SHIFT;
	} else {
		/* if not using ROCit, presume the v1 memory map */
		mem_map = MEM_MAP_V1;
	}

	switch (mem_map) {
	case MEM_MAP_V1:
		mem_regions = malta_mem_regions_v1;
		break;
	case MEM_MAP_V2:
		mem_regions = malta_mem_regions_v2;
		break;
	default:
		pr_err("Unsupported physical memory map v%u detected\n",
		       (unsigned int)mem_map);
		return -EINVAL;
	}

	return yamon_dt_append_memory(fdt, mem_regions);
}

static __init int dt_remove_gic(void *fdt)
{
	int err, gic_off, i8259_off, cpu_off;
	uint32_t cpu_phandle;

	/* if a GIC is present, do nothing to the DT */
	if (malta_has_gic)
		return 0;

	gic_off = fdt_node_offset_by_compatible(fdt, -1, "mti,gic");
	if (gic_off < 0) {
		pr_warn("unable to find DT GIC node: %d\n", gic_off);
		return gic_off;
	}

	err = fdt_nop_node(fdt, gic_off);
	if (err)
		pr_warn("unable to nop GIC node\n");

	i8259_off = fdt_node_offset_by_compatible(fdt, -1, "intel,i8259");
	if (i8259_off < 0) {
		pr_warn("unable to find DT i8259 node: %d\n", i8259_off);
		return i8259_off;
	}

	cpu_off = fdt_node_offset_by_compatible(fdt, -1,
			"mti,cpu-interrupt-controller");
	if (cpu_off < 0) {
		pr_warn("unable to find CPU intc node: %d\n", cpu_off);
		return cpu_off;
	}

	cpu_phandle = fdt_get_phandle(fdt, cpu_off);
	if (!cpu_phandle) {
		pr_warn("unable to get CPU intc phandle\n");
		return -EINVAL;
	}

	err = fdt_setprop_u32(fdt, i8259_off, "interrupt-parent", cpu_phandle);
	if (err) {
		pr_warn("unable to set i8259 interrupt-parent: %d\n", err);
		return err;
	}

	err = fdt_setprop_u32(fdt, i8259_off, "interrupts", 2);
	if (err) {
		pr_warn("unable to set i8259 interrupts: %d\n", err);
		return err;
	}

	return 0;
}

static __init uint64_t malta_gic_count(void __iomem *gic_base)
{
	unsigned int hi, hi2, lo;

	if (mips_cm_is64)
		return __raw_readq(gic_base + GIC_REG(SHARED, GIC_SH_COUNTER));

	do {
		hi = __raw_readl(gic_base + GIC_REG(SHARED, GIC_SH_COUNTER_63_32));
		lo = __raw_readl(gic_base + GIC_REG(SHARED, GIC_SH_COUNTER_31_00));
		hi2 = __raw_readl(gic_base + GIC_REG(SHARED, GIC_SH_COUNTER_63_32));
	} while (hi2 != hi);

	return (((uint64_t)hi) << 32) + lo;
}

static __init void measure_freq(const void *fdt, unsigned long *cpu,
				unsigned long *gic)
{
	uint32_t cp0_count, cp0_start, gic_cfg;
	uint64_t gic_count = 0, gic_start = 0;
	void __iomem *gic_base = NULL;
	unsigned char secs1, secs2, ctrl;
	unsigned long phys_base;
	int secs, gic_off;

#if defined(CONFIG_KVM_GUEST) && CONFIG_KVM_GUEST_TIMER_FREQ
	*cpu = CONFIG_KVM_GUEST_TIMER_FREQ * 1000000;
	*gic = 0;
	return;
#endif

	if (malta_has_gic) {
		gic_off = fdt_node_offset_by_compatible(fdt, -1, "mti,gic");
		if (gic_off < 0)
			pr_warn("GIC present but can't find DT node: %d\n", gic_off);
		else {
			/* Find the GIC base address */
			phys_base = fdt_get_address(fdt, gic_off, NULL);

			/* Map & enable the GIC if necessary */
			if (mips_cm_present()) {
				write_gcr_gic_base(phys_base | CM_GCR_GIC_BASE_GICEN_MSK);

				/* Ensure the GIC is enabled before we attempt access */
				mb();
			}

			gic_base = ioremap_nocache(phys_base, SHARED_SECTION_SIZE);
		}
	}

	if (gic_base) {
		/* if a GIC is present ensure that its counter isn't stopped */
		gic_cfg = __raw_readl(gic_base + GIC_REG(SHARED, GIC_SH_CONFIG));
		gic_cfg &= ~GIC_SH_CONFIG_COUNTSTOP_MSK;
		__raw_writel(gic_cfg, gic_base + GIC_REG(SHARED, GIC_SH_CONFIG));
	}

	/*
	 * Read counters exactly on rising edge of update flag.
	 * This helps get an accurate reading under virtualisation.
	 */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));
	cp0_start = read_c0_count();
	if (gic_base)
		gic_start = malta_gic_count(gic_base);

	/* Wait for falling edge before reading RTC. */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	secs1 = CMOS_READ(RTC_SECONDS);

	/* Read counters again exactly on rising edge of update flag. */
	while (!(CMOS_READ(RTC_REG_A) & RTC_UIP));
	cp0_count = read_c0_count();
	if (gic_base)
		gic_count = malta_gic_count(gic_base);

	/* Wait for falling edge before reading RTC again. */
	while (CMOS_READ(RTC_REG_A) & RTC_UIP);
	secs2 = CMOS_READ(RTC_SECONDS);

	ctrl = CMOS_READ(RTC_CONTROL);

	if (!(ctrl & RTC_DM_BINARY)) {
		secs1 = bcd2bin(secs1);
		secs2 = bcd2bin(secs2);
	}
	secs = secs2 - secs1;
	if (secs < 1)
		secs += 60;

	*cpu = (cp0_count - cp0_start) / secs;
	if (gic_base)
		*gic = div_u64(gic_count - gic_start, secs);
	else
		*gic = 0;
}

static __init int dt_clock_freq(void *fdt)
{
	unsigned long cpu_freq, gic_freq;
	int cpu_off, gic_off, err;

	measure_freq(fdt, &cpu_freq, &gic_freq);

	switch (boot_cpu_type()) {
	case CPU_20KC:
	case CPU_25KF:
		/* The counter runs at the CPU clock rate */
		break;
	default:
		/* The counter runs at half the CPU clock rate */
		cpu_freq *= 2;
		break;
	}

	cpu_off = fdt_node_offset_by_compatible(fdt, -1, "img,mips");
	if (cpu_off < 0) {
		pr_warn("unable to find CPU node: %d\n", cpu_off);
		return cpu_off;
	}

	err = fdt_setprop_u32(fdt, cpu_off, "clock-frequency", cpu_freq);
	if (err) {
		pr_warn("unable to set CPU clock-frequency: %d\n", err);
		return err;
	}

	if (malta_has_gic) {
		gic_off = fdt_node_offset_by_compatible(fdt, -1, "mti,gic-timer");
		if (gic_off < 0) {
			pr_warn("unable to find GIC timer node: %d\n", gic_off);
			return gic_off;
		}

		err = fdt_setprop_u32(fdt, gic_off, "clock-frequency", gic_freq);
		if (err) {
			pr_warn("unable to set CPU clock-frequency: %d\n", err);
			return err;
		}
	}

	return 0;
}

static __init void read_gt64120_range(void __iomem *gt_base,
				      unsigned int low, unsigned int high,
				      uint32_t *pstart, uint32_t *psize)
{
	uint32_t start, end;

	start = readl(gt_base + low);

	end = readl(gt_base + high);
	end |= start & GENMASK(14, 7);

	start <<= 21;
	end <<= 21;
	end |= GENMASK(20, 0);

	*pstart = start;
	*psize = end + 1 - start;
}

static __init int dt_gt64120(void *fdt)
{
	void __iomem *gt_base;
	int off, err;
	uint32_t m0_lo, m0_sz, m1_lo, m1_sz, io_lo, io_sz;
	uint32_t ranges[6 * 2];

	/* if we're not using a GT-64120 do nothing to the DT */
	if (malta_syscon != MIPS_REVISION_SCON_GT64120)
		return 0;

	off = fdt_node_offset_by_compatible(fdt, -1, "galileo,gt-64120");
	if (off < 0) {
		pr_err("unable to find GT-64120 DT node: %d\n", off);
		return off;
	}

	gt_base = ioremap_nocache(fdt_get_address(fdt, off, NULL),
				  GT64120_SIZE);
	if (!gt_base) {
		pr_err("unable to map GT-64120 I/O\n");
		return -ENOMEM;
	}

	err = fdt_setprop_string(fdt, off, "status", "okay");
	if (err) {
		pr_warn("unable to enable GT-64120: %d\n", err);
		return err;
	}

	off = fdt_node_offset_by_compatible(fdt, off, "galileo,gt-64120-pci");
	if (off < 0) {
		pr_err("unable to find GT-64120 PCI DT node: %d\n", off);
		return off;
	}

	read_gt64120_range(gt_base,
			   GT64120_PCI0_IO_LOW, GT64120_PCI0_IO_HIGH,
			   &io_lo, &io_sz);
	read_gt64120_range(gt_base,
			   GT64120_PCI0_M0_LOW, GT64120_PCI0_M0_HIGH,
			   &m0_lo, &m0_sz);
	read_gt64120_range(gt_base,
			   GT64120_PCI0_M1_LOW, GT64120_PCI0_M1_HIGH,
			   &m1_lo, &m1_sz);

	/* I/O PCI address */
	ranges[0] = cpu_to_be32(1 << 24);
	ranges[1] = 0;
	ranges[2] = 0;

	/* I/O CPU address: 0 because we offset by mips_io_port_base */
	ranges[3] = 0;

	/* I/O size */
	ranges[4] = 0;
	ranges[5] = cpu_to_be32(io_sz);

	/* Memory PCI address */
	ranges[6] = cpu_to_be32(2 << 24);
	ranges[7] = 0;
	ranges[8] = 0;

	/* Memory CPU address */
	ranges[9] = cpu_to_be32((m1_sz > m0_sz) ? m1_lo : m0_lo);

	/* Memory size */
	ranges[10] = 0;
	ranges[11] = cpu_to_be32((m1_sz > m0_sz) ? m1_sz : m0_sz);

	err = fdt_setprop(fdt, off, "ranges", ranges, sizeof(ranges));
	if (err) {
		pr_err("unable to write GT-64120 ranges: %d\n", err);
		return err;
	}

	return 0;
}

static __init int dt_msc01(void *fdt)
{
	void __iomem *msc_base;
	int off, err;
	uint32_t m_bas, m_msk, m_map, m_sz, m_end;
	uint32_t io_bas, io_msk, io_map, io_sz, io_end;
	uint32_t ranges[6 * 2], reg[2], mask;
	const uint32_t *orig_reg;
	int reg_len;

	off = fdt_node_offset_by_compatible(fdt, -1, "mti,msc01");
	if (off < 0) {
		pr_err("unable to find MSC01 DT node: %d\n", off);
		return off;
	}

	orig_reg = fdt_getprop(fdt, off, "reg", &reg_len);
	if (!orig_reg || reg_len != sizeof(reg)) {
		pr_err("invalid MSC01 reg property\n");
		return -EINVAL;
	}

	memcpy(reg, orig_reg, sizeof(reg));

	switch (malta_syscon) {
	case MIPS_REVISION_SCON_SOCIT:
	case MIPS_REVISION_SCON_ROCIT:
		break;

	case MIPS_REVISION_SCON_SOCITSC:
	case MIPS_REVISION_SCON_SOCITSCP:
		reg[0] = cpu_to_be32(MSC01_PCI_BASE_SOCITSC);
		err = fdt_setprop_inplace(fdt, off, "reg", reg, sizeof(reg));
		if (err)
			pr_warn("unable to set MSC01 reg property: %d\n", err);
		break;

	default:
		/*
		 * We're not using an MSC01 based system controller, so do
		 * nothing to the device tree.
		 */
		return 0;
	}

	msc_base = ioremap_nocache(be32_to_cpu(reg[0]), MSC01_PCI_SIZE);
	if (!msc_base) {
		pr_err("unable to map MSC01 registers\n");
		return -ENOMEM;
	}

	err = fdt_setprop_string(fdt, off, "status", "okay");
	if (err) {
		pr_warn("unable to enable GT-64120: %d\n", err);
		return err;
	}

	off = fdt_node_offset_by_compatible(fdt, off, "mti,msc01-pci");
	if (off < 0) {
		pr_err("unable to find MSC01 PCI DT node: %d\n", off);
		return off;
	}

	/*
	 * Setup the Malta max (2GB) memory for PCI DMA in host bridge
	 * in transparent addressing mode.
	 */
	mask = PHYS_OFFSET | PCI_BASE_ADDRESS_MEM_PREFETCH;
	__raw_writel(mask, msc_base + MSC01_PCI_BAR0);
	__raw_writel(mask, msc_base + MSC01_PCI_HEAD4);

	mask &= MSC01_PCI_BAR0_SIZE;
	__raw_writel(mask, msc_base + MSC01_PCI_P2SCMSKL);
	__raw_writel(mask, msc_base + MSC01_PCI_P2SCMAPL);

	io_bas = __raw_readl(msc_base + MSC01_PCI_SC2PIOBASL);
	io_msk = __raw_readl(msc_base + MSC01_PCI_SC2PIOMSKL);
	io_map = __raw_readl(msc_base + MSC01_PCI_SC2PIOMAPL);
	io_sz = (~io_msk) + 1;
	io_end = io_bas + io_sz;

	m_bas = __raw_readl(msc_base + MSC01_PCI_SC2PMBASL);
	m_msk = __raw_readl(msc_base + MSC01_PCI_SC2PMMSKL);
	m_map = __raw_readl(msc_base + MSC01_PCI_SC2PMMAPL);
	m_sz = (~m_msk) + 1;
	m_end = m_bas + m_sz;

	if (((io_bas >= m_bas) && (io_bas < m_end)) ||
	    ((io_end >= m_bas) && (io_end < m_end))) {
		/*
		 * The memory & I/O regions overlap. I/O takes priority, so we
		 * handle this by shrinking the memory region to its largest
		 * subregion that doesn't overlap with I/O.
		 */
		if (max(io_bas, m_bas) - m_bas >= m_end - min(io_end, m_end))
			m_end = io_bas;
		else
			m_bas = io_end;

		m_sz = m_end - m_bas;
	}

	/* I/O PCI address */
	ranges[0] = cpu_to_be32(1 << 24);
	ranges[1] = 0;
	ranges[2] = 0;

	/* I/O CPU address: 0 because we offset by mips_io_port_base */
	ranges[3] = 0;

	/* I/O size */
	ranges[4] = 0;
	ranges[5] = cpu_to_be32(io_sz);

	/* Memory PCI address */
	ranges[6] = cpu_to_be32(2 << 24);
	ranges[7] = 0;
	ranges[8] = 0;

	/* Memory CPU address */
	ranges[9] = cpu_to_be32(m_bas & m_msk);

	/* Memory size */
	ranges[10] = 0;
	ranges[11] = cpu_to_be32(m_sz);

	err = fdt_setprop(fdt, off, "ranges", ranges, sizeof(ranges));
	if (err) {
		pr_err("unable to write MSC01 ranges: %d\n", err);
		return err;
	}

	return 0;
}

static __init int dt_dma_coherence(void *fdt)
{
	uint32_t cfg, val;
	int err;

	if (mips_cm_numiocu() == 0)
		return 0;

	cfg = __raw_readl((u32 *)ROCIT_CONFIG_GEN0);
	if (!(cfg & ROCIT_CONFIG_GEN0_PCI_IOCU))
		return 0;

	val = cpu_to_be32(1);
	err = fdt_setprop(fdt, 0, "dma-coherent", &val, sizeof(val));
	if (err) {
		pr_err("unable to set dma-coherent: %d\n", err);
		return err;
	}

	return 0;
}

static const struct mips_fdt_fixup malta_fdt_fixups[] __initconst = {
	{ yamon_dt_append_cmdline, "append command line" },
	{ dt_append_memory, "append memory" },
	{ yamon_dt_serial_config, "append serial configuration" },
	{ dt_remove_gic, "remove GIC if not present" },
	{ dt_clock_freq, "estimate clock frequencies" },
	{ dt_gt64120, "configure GT-64120 system controller" },
	{ dt_msc01, "configure MSC01 system controller" },
	{ dt_dma_coherence, "configure cache-coherent DMA" },
	{ },
};

static __init const void *malta_fixup_fdt(const void *fdt,
					  const void *match_data)
{
	static unsigned char fdt_buf[16 << 10] __initdata;
	int err;

	if (fdt_check_header(fdt))
		panic("Corrupt DT");

	/* if this isn't Malta, something went wrong */
	BUG_ON(fdt_node_check_compatible(fdt, 0, "mti,malta"));

	fw_init_cmdline();

	err = apply_mips_fdt_fixups(fdt_buf, sizeof(fdt_buf),
				    fdt, malta_fdt_fixups);
	if (err)
		panic("Unable to fixup FDT: %d", err);

	return fdt_buf;
}

extern char __dtb_malta_begin[];

MIPS_MACHINE(malta) = {
	.fdt = __dtb_malta_begin,
	.detect = malta_detect,
	.prom_init = malta_prom_init,
	.fixup_fdt = malta_fixup_fdt,
};
