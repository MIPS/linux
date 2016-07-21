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

static void __iomem *gt64120_iack_reg;

struct gt64120_pci {
	void __iomem *base;
	struct device *dev;
};

enum gt64120_reg {
	REG_PCI0IOLD			= 0x048,
	REG_PCI0IOREMAP			= 0x0f0,

	REG_PCI0_CMD			= 0xc00,
#define REG_PCI0_CMD_MBYTESWAP		BIT(0)
#define REG_PCI0_CMD_SBYTESWAP		BIT(16)

	REG_INTRCAUSE			= 0xc18,
#define REG_INTRCAUSE_MASABORT0		BIT(18)
#define REG_INTRCAUSE_TARABORT0		BIT(19)

	REG_PCI0_IACK			= 0xc34,

	REG_PCI0_CFGADDR		= 0xcf8,
#define REG_PCI0_CFGADDR_REGNUM_SHF	2
#define REG_PCI0_CFGADDR_FUNCTNUM_SHF	8
#define REG_PCI0_CFGADDR_DEVNUM_SHF	11
#define REG_PCI0_CFGADDR_BUSNUM_SHF	16
#define REG_PCI0_CFGADDR_CONFIGEN	BIT(31)

	REG_PCI0_CFGDATA		= 0xcfc,
};

static int gt64120_pci_config_setup(struct pci_bus *bus, unsigned int devfn,
				    int where)
{
	struct gt64120_pci *gtpci = bus->sysdata;

	/* Because of a bug in the galileo (for slot 31) */
	if ((bus->number == 0) && (devfn >= PCI_DEVFN(31, 0)))
		return -ENODEV;

	/* Clear pending interrupts */
	writel((u32)~(REG_INTRCAUSE_MASABORT0 | REG_INTRCAUSE_TARABORT0),
	       gtpci->base + REG_INTRCAUSE);

	/* Setup address */
	writel((bus->number << REG_PCI0_CFGADDR_BUSNUM_SHF) |
	       (devfn << REG_PCI0_CFGADDR_FUNCTNUM_SHF) |
	       ((where / 4) << REG_PCI0_CFGADDR_REGNUM_SHF) |
	       REG_PCI0_CFGADDR_CONFIGEN,
	       gtpci->base + REG_PCI0_CFGADDR);

	return 0;
}

static int gt64120_pci_config_read(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 *val)
{
	struct gt64120_pci *gtpci = bus->sysdata;
	unsigned int data, intr;
	int err;

	err = gt64120_pci_config_setup(bus, devfn, where);
	if (err)
		return PCIBIOS_DEVICE_NOT_FOUND;

	data = __raw_readl(gtpci->base + REG_PCI0_CFGDATA);

	if (bus->number == 0 && PCI_SLOT(devfn) == 0)
		data = le32_to_cpu(data);

	if (size < 4) {
		data >>= (where & 0x3) * BITS_PER_BYTE;
		data &= GENMASK((size * BITS_PER_BYTE) - 1, 0);
	}

	*val = data;

	/* Check for master or target abort */
	intr = readl(gtpci->base +  REG_INTRCAUSE);
	if (intr & (REG_INTRCAUSE_MASABORT0 | REG_INTRCAUSE_TARABORT0))
		return PCIBIOS_SET_FAILED;

	return PCIBIOS_SUCCESSFUL;
}

static int gt64120_pci_config_write(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 val)
{
	struct gt64120_pci *gtpci = bus->sysdata;
	unsigned int bit_off, intr, data = val;
	int err;

	err = gt64120_pci_config_setup(bus, devfn, where);
	if (err)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (size < 4) {
		data = __raw_readl(gtpci->base + REG_PCI0_CFGDATA);

		if (bus->number == 0 && PCI_SLOT(devfn) == 0)
			data = le32_to_cpu(data);

		bit_off = (where & 0x3) * BITS_PER_BYTE;
		data &= ~(GENMASK((size * BITS_PER_BYTE) - 1, 0) << bit_off);
		data |= val << bit_off;
	}

	if (bus->number == 0 && PCI_SLOT(devfn) == 0)
		data = le32_to_cpu(data);

	__raw_writel(data, gtpci->base + REG_PCI0_CFGDATA);

	/* Check for master or target abort */
	intr = readl(gtpci->base +  REG_INTRCAUSE);
	if (intr & (REG_INTRCAUSE_MASABORT0 | REG_INTRCAUSE_TARABORT0))
		return PCIBIOS_SET_FAILED;

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops gt64120_pci_ops = {
	.read	= gt64120_pci_config_read,
	.write	= gt64120_pci_config_write,
};

static inline int gt64120_iack(void)
{
	return readl(gt64120_iack_reg) & 0xff;
}

static int gt64120_probe(struct platform_device *pdev)
{
	struct gt64120_pci *gtpci;
	struct device *dev = &pdev->dev;
	struct pci_bus *bus;
	resource_size_t iobase = 0;
	LIST_HEAD(res);
	struct resource reg_res;
	int err;

	if (!dev->of_node)
		return -ENODEV;

	if (!dev->parent || !dev->parent->of_node)
		return -ENODEV;

	gtpci = devm_kzalloc(dev, sizeof(*gtpci), GFP_KERNEL);
	if (!gtpci)
		return -ENOMEM;

	gtpci->dev = dev;

	err = of_address_to_resource(dev->parent->of_node, 0, &reg_res);
	if (err)
		return -ENOMEM;

	gtpci->base = devm_ioremap_resource(dev, &reg_res);
	if (!gtpci->base)
		return -ENOMEM;

#ifdef CONFIG_CPU_LITTLE_ENDIAN
	writel(REG_PCI0_CMD_MBYTESWAP | REG_PCI0_CMD_SBYTESWAP,
	       gtpci->base + REG_PCI0_CMD);
#else
	writel(0, gtpci->base + REG_PCI0_CMD);
#endif

	/* Setup i8259 interrupt polling via IACK register */
	if (!gt64120_iack_reg) {
		gt64120_iack_reg = gtpci->base + REG_PCI0_IACK;
		i8259_set_poll(gt64120_iack);
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
				  &gt64120_pci_ops, gtpci, &res);
	if (!bus)
		return -ENOMEM;

	pci_scan_child_bus(bus);
	pci_assign_unassigned_bus_resources(bus);
	pci_fixup_irqs(pci_common_swizzle, of_irq_parse_and_map_pci);
	pci_bus_add_devices(bus);
	platform_set_drvdata(pdev, gtpci);

	return 0;
}

static struct of_device_id gt64120_of_match[] = {
	{ .compatible = "galileo,gt-64120-pci", },
	{}
};

static struct platform_driver gt64120_driver = {
	.driver = {
		.name = "gt64120-pci",
		.of_match_table = gt64120_of_match,
	},
	.probe = gt64120_probe,
};
module_platform_driver(gt64120_driver);

MODULE_AUTHOR("Paul Burton");
MODULE_DESCRIPTION("Galileo GT-64120 PCI Controller Driver");
MODULE_LICENSE("GPL v2");
