/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <asm/i8259.h>

static void __iomem *msc01_iack_reg;

struct msc01_pci {
	void __iomem *base;
	struct device *dev;
};

enum msc01_reg {
	REG_INTSTAT			= 0x608,
#define REG_INTSTAT_TA			BIT(6)
#define REG_INTSTAT_MA			BIT(7)

	REG_CFGADDR			= 0x610,
#define REG_CFGADDR_BUS_SHIFT		16
#define REG_CFGADDR_DEV_SHIFT		11
#define REG_CFGADDR_FUNC_SHIFT		8
#define REG_CFGADDR_REG_SHIFT		0

	REG_CFGDATA			= 0x618,
	REG_IACK			= 0x620,

	REG_CFG				= 0x2380,
#define REG_CFG_MAXRETRY		0xfff
#define REG_CFG_MAXRETRY_SHIFT		0
#define REG_CFG_EN			BIT(15)

	REG_SWAP			= 0x2388,
#define REG_SWAP_BAR0			BIT(0)
#define REG_SWAP_MEM			BIT(16)
#define REG_SWAP_IO			BIT(18)
};

static int do_config_access(struct pci_bus *bus, unsigned int devfn,
			    int where, uint32_t *data, bool write)
{
	struct msc01_pci *mscpci = bus->sysdata;
	uint32_t addr, intstat;

	/* Clear any pending abort interrupts */
	__raw_writel(REG_INTSTAT_TA | REG_INTSTAT_MA,
		     mscpci->base + REG_INTSTAT);

	/* Set up the config space address */
	addr = bus->number << REG_CFGADDR_BUS_SHIFT;
	addr |= PCI_SLOT(devfn) << REG_CFGADDR_DEV_SHIFT;
	addr |= PCI_FUNC(devfn) << REG_CFGADDR_FUNC_SHIFT;
	addr |= where << REG_CFGADDR_REG_SHIFT;
	__raw_writel(addr, mscpci->base + REG_CFGADDR);

	/* Perform the access */
	if (write)
		__raw_writel(*data, mscpci->base + REG_CFGDATA);
	else
		*data = __raw_readl(mscpci->base + REG_CFGDATA);

	/* Detect aborts */
	intstat = __raw_readl(mscpci->base + REG_INTSTAT);
	if (intstat & (REG_INTSTAT_TA | REG_INTSTAT_MA))
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int msc01_pci_config_read(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 *val)
{
	int err;

	err = do_config_access(bus, devfn, where & ~0x3, val, false);
	if (err)
		return err;

	if (size < 4)
		*val = (*val >> (8 * (where & 3))) & ((1 << (size * 8)) - 1);

	return PCIBIOS_SUCCESSFUL;
}

static int msc01_pci_config_write(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 val)
{
	uint32_t data;
	int err;

	if (size == 4) {
		data = val;
	} else {
		err = do_config_access(bus, devfn, where & ~0x3, &data, false);
		if (err)
			return err;

		data &= ~(((1 << (size * 8)) - 1) << ((where & 0x3) * 8));
		data |= val << ((where & 0x3) * 8);
	}

	return do_config_access(bus, devfn, where & ~0x3, &data, true);
}

static struct pci_ops msc01_pci_ops = {
	.read	= msc01_pci_config_read,
	.write	= msc01_pci_config_write,
};

static inline int msc01_iack(void)
{
	return __raw_readl(msc01_iack_reg) & 0xff;
}

static int msc01_probe(struct platform_device *pdev)
{
	struct msc01_pci *mscpci;
	struct device *dev = &pdev->dev;
	struct pci_bus *bus;
	resource_size_t iobase = 0;
	LIST_HEAD(res);
	struct resource reg_res;
	uint32_t cfg;
	int err;

	if (!dev->of_node)
		return -ENODEV;

	if (!dev->parent || !dev->parent->of_node)
		return -ENODEV;

	mscpci = devm_kzalloc(dev, sizeof(*mscpci), GFP_KERNEL);
	if (!mscpci)
		return -ENOMEM;

	mscpci->dev = dev;

	err = of_address_to_resource(dev->parent->of_node, 0, &reg_res);
	if (err)
		return -ENOMEM;

	mscpci->base = devm_ioremap_resource(dev, &reg_res);
	if (!mscpci->base)
		return -ENOMEM;

	cfg = __raw_readl(mscpci->base + REG_CFG);

	/* Disable the PCI controller */
	__raw_writel(cfg & ~REG_CFG_EN, mscpci->base + REG_CFG);

	/* Ensure the controller is disabled before we try to configure it */
	mb();

#ifdef CONFIG_CPU_LITTLE_ENDIAN
	__raw_writel(0, mscpci->base + REG_SWAP);
#else
	__raw_writel(REG_SWAP_BAR0 | REG_SWAP_MEM | REG_SWAP_IO,
		     mscpci->base + REG_SWAP);
#endif

	/* Allow retries, but not infinite retries */
	cfg &= ~REG_CFG_MAXRETRY;
	cfg |= REG_CFG_MAXRETRY - 1;

	/*
	 * Ensure previous register writes complete before we re-enable the PCI
	 * controller.
	 */
	wmb();

	/* Re-enable the PCI controller */
	__raw_writel(cfg, mscpci->base + REG_CFG);

	/* Ensure the controller is re-enabled before we try to use it */
	mb();

	/* Setup i8259 interrupt polling via IACK register */
	if (!msc01_iack_reg) {
		msc01_iack_reg = mscpci->base + REG_IACK;
		i8259_set_poll(msc01_iack);
	} else {
		dev_warn(dev, "IACK already setup - multiple instances?\n");
	}

	err = of_pci_get_host_bridge_resources(dev->of_node, 0, 0xff, &res,
					       &iobase);
	if (err) {
		dev_err(dev, "Failed to get bridge resources\n");
		return err;
	}

	bus = pci_create_root_bus(&pdev->dev, 0,
				  &msc01_pci_ops, mscpci, &res);
	if (!bus)
		return -ENOMEM;

	pci_scan_child_bus(bus);
	pci_assign_unassigned_bus_resources(bus);
	pci_fixup_irqs(pci_common_swizzle, of_irq_parse_and_map_pci);
	pci_bus_add_devices(bus);
	platform_set_drvdata(pdev, mscpci);

	return 0;
}

static struct of_device_id msc01_of_match[] = {
	{ .compatible = "mti,msc01-pci", },
	{}
};

static struct platform_driver msc01_driver = {
	.driver = {
		.name = "msc01-pci",
		.of_match_table = msc01_of_match,
	},
	.probe = msc01_probe,
};
module_platform_driver(msc01_driver);

MODULE_AUTHOR("Paul Burton");
MODULE_DESCRIPTION("MIPS MSC01 PCI Controller Driver");
MODULE_LICENSE("GPL v2");
