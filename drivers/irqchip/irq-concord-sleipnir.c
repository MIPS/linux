/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) "concord-sleipnir: " fmt

#include <linux/errno.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/printk.h>

#define CS_NUM_IRQS			4

#define XILINX_PCIE_REG_IDR		0x138
#define XILINX_PCIE_REG_IMR		0x13c
#define  XILINX_PCIE_REG_IMR_INTX	BIT(16)
#define XILINX_PCIE_REG_RPEFR		0x154
#define  XILINX_PCIE_RPEFR_ERR_VALID	BIT(18)
#define XILINX_PCIE_REG_RPIFR1		0x158
#define  XILINX_PCIE_REG_RPIFR1_ASSERT	BIT(29)
#define  XILINX_PCIE_REG_RPIFR1_VALID	BIT(31)

#define INT_PENDING			0x00
#define INT_ENABLE			0x04

struct concord_sleipnir_ctx {
	int parent_irq;
	struct irq_domain *domain;
	void __iomem *regs, *int_regs;
};

static void concord_sleipnir_irq_handler(struct irq_desc *desc)
{
	struct concord_sleipnir_ctx *ctx = irq_desc_get_handler_data(desc);
	u32 intr, fifo_entry, enable;
	DECLARE_BITMAP(pending, CS_NUM_IRQS);
	int irq;

	intr = readl(ctx->regs + XILINX_PCIE_REG_IDR);
	enable = readl(ctx->int_regs + INT_ENABLE);

	while (intr & (0x7 << 9)) {
		fifo_entry = readl(ctx->regs + XILINX_PCIE_REG_RPEFR);
		if (!(fifo_entry & XILINX_PCIE_RPEFR_ERR_VALID))
			break;

		writel(~0, ctx->regs + XILINX_PCIE_REG_RPEFR);
	}

	while (intr & XILINX_PCIE_REG_IMR_INTX) {
		/* read & remove the interrupt from the interrupt FIFO */
		fifo_entry = readl(ctx->regs + XILINX_PCIE_REG_RPIFR1);

		if (!(fifo_entry & XILINX_PCIE_REG_RPIFR1_VALID))
			break;

		writel(~0, ctx->regs + XILINX_PCIE_REG_RPIFR1);

		if (!(fifo_entry & XILINX_PCIE_REG_RPIFR1_ASSERT))
			continue;

		pending[0] = readl(ctx->int_regs + INT_PENDING);
		writel(0, ctx->int_regs + INT_ENABLE);

		/* chain the IRQ */
		for_each_set_bit(irq, pending, CS_NUM_IRQS)
			generic_handle_irq(irq_linear_revmap(ctx->domain, irq));
	}

	/* clear the interrupt decode register */
	writel(intr, ctx->regs + XILINX_PCIE_REG_IDR);
	writel(enable, ctx->int_regs + INT_ENABLE);
}

static int concord_sleipnir_irqd_map(struct irq_domain *d, unsigned int irq,
				     irq_hw_number_t hw)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);

	return 0;
}

static const struct irq_domain_ops concord_sleipnir_irqd_ops = {
	.xlate = irq_domain_xlate_onetwocell,
	.map = concord_sleipnir_irqd_map,
};

static int __init concord_sleipnir_of_init(struct device_node *node,
					   struct device_node *parent)
{
	struct concord_sleipnir_ctx *ctx;
	int err = -ENOMEM;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		goto err_out;

	ctx->regs = of_iomap(node, 0);
	if (!ctx->regs) {
		pr_err("unable to map registers\n");
		err = -ENXIO;
		goto err_free_ctx;
	}

	ctx->int_regs = of_iomap(node, 1);
	if (!ctx->regs) {
		pr_err("unable to map registers\n");
		err = -ENXIO;
		goto err_free_ctx;
	}

	ctx->domain = irq_domain_add_linear(node, CS_NUM_IRQS,
					    &concord_sleipnir_irqd_ops, ctx);
	if (!ctx->domain) {
		pr_err("unable to add IRQ domain\n");
		err = -ENXIO;
		goto err_free_ctx;
	}

	ctx->parent_irq = irq_of_parse_and_map(node, 0);
	if (!ctx->parent_irq) {
		pr_err("unable to map parent IRQ\n");
		err = -EINVAL;
		goto err_remove_domain;
	}

	irq_set_chained_handler_and_data(ctx->parent_irq,
					 concord_sleipnir_irq_handler,
					 ctx);

	/* Unmask all interrupts */
	writel(GENMASK(CS_NUM_IRQS - 1, 0), ctx->int_regs + INT_ENABLE);

	/* Enable INTx interrupts */
	writel(XILINX_PCIE_REG_IMR_INTX | (7 << 9),
	       ctx->regs + XILINX_PCIE_REG_IMR);

	return 0;

err_remove_domain:
	irq_domain_remove(ctx->domain);
err_free_ctx:
	kfree(ctx);
err_out:
	return err;
}

IRQCHIP_DECLARE(concord_sleipnir, "img,concord-sleipnir-passthrough",
		concord_sleipnir_of_init);
