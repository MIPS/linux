/*
 * Copyright (C) 2017 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/edac.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <asm/mips-cpc.h>

#include "edac_device.h"

enum mips_cpc_fault {
	FAULT_INTEGRITY = 0,
	FAULT_RAM_DATA_CORR,
	FAULT_RAM_DATA_UNCORR,
	FAULT_RAM_ADDR,
	FAULT_DPAR,
	FAULT_APAR,
	FAULT_PAR_REG_CONTROL,
	FAULT_PAR_REG_STATUS,
	FAULT_TIMEOUT,
	FAULT_PROTOCOL,
	FAULT_LBIST,
	FAULT_MBIST,

	/* Number of fault bits */
	FAULT_COUNT,
};

static const char *fault_names[FAULT_COUNT] = {
	[FAULT_INTEGRITY]	= "integrity check",
	[FAULT_RAM_DATA_CORR]	= "correctable RAM data",
	[FAULT_RAM_DATA_UNCORR]	= "uncorrectable RAM data",
	[FAULT_RAM_ADDR]	= "RAM address",
	[FAULT_DPAR]		= "data path parity",
	[FAULT_APAR]		= "address path parity",
	[FAULT_PAR_REG_CONTROL]	= "control register parity",
	[FAULT_PAR_REG_STATUS]	= "status register parity",
	[FAULT_TIMEOUT]		= "transaction timeout",
	[FAULT_PROTOCOL]	= "interface protocol",
	[FAULT_LBIST]		= "logic BIST",
	[FAULT_MBIST]		= "memory BIST",
};

static unsigned int mips_cpc_fault_report(struct edac_device_ctl_info *edac,
					  unsigned int cluster,
					  unsigned int core)
{
	void (*handle_fn)(struct edac_device_ctl_info *, int, int, const char *);
	unsigned int fault, count, other;
	unsigned long status;

	other = CM3_GCR_Cx_REDIRECT_CLUSTER_REDIREN_MSK;
	other |= cluster << CM3_GCR_Cx_REDIRECT_CLUSTER_SHF;
	other |= core << CM3_GCR_Cx_OTHER_CORE_SHF;
	write_gcr_cl_other(other);
	mb();

	status = read_cpc_co_fault_status();
	if (!status)
		return 0;

	count = 0;
	for_each_set_bit(fault, &status, FAULT_COUNT) {
		count++;

		if (fault == FAULT_RAM_DATA_CORR)
			handle_fn = &edac_device_handle_ce;
		else
			handle_fn = &edac_device_handle_ue;

		handle_fn(edac, 0, 0, fault_names[fault]);
	}

	write_cpc_co_fault_clear(status);
	return count;
}

static irqreturn_t mips_cpc_fault_irq(int irq, void *_edac)
{
	struct edac_device_ctl_info *edac = _edac;
	unsigned int cluster, core, faults, other;

	dev_err(edac->dev, "FuSa fault interrupt occurred\n");

	faults = 0;
	other = read_gcr_cl_other();

	for_each_possible_cluster(cluster) {
		faults += mips_cpc_fault_report(edac, cluster, 0x20);

		for (core = 0; core < mips_cm_numcores(cluster); core++)
			faults += mips_cpc_fault_report(edac, cluster, core);
	}

	write_gcr_cl_other(other);

	return faults ? IRQ_HANDLED : IRQ_NONE;
}

static int mips_cpc_fault_probe(struct platform_device *pdev)
{
	struct edac_device_ctl_info *edac;
	struct device_node *np;
	int err, irq;

	np = pdev->dev.of_node;

	edac = edac_device_alloc_ctl_info(0, "MIPS CPS", 1, "FuSa", 1,
					  0, NULL, 0,
					  edac_device_alloc_index());
	if (!edac) {
		dev_err(&pdev->dev, "Unable to allocate EDAC device\n");
		return -ENODEV;
	}

	edac->dev = &pdev->dev;
	edac->panic_on_ue = 1;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Unable to map fault IRQ\n");
		return -ENODEV;
	}

	err = devm_request_irq(&pdev->dev, irq, mips_cpc_fault_irq,
			       IRQF_TRIGGER_HIGH, "mips-cpc-fault", edac);
	if (err) {
		dev_err(&pdev->dev, "Unable to request fault IRQ: %d\n", err);
		return err;
	}

	err = edac_device_add_device(edac);
	if (err) {
		dev_err(&pdev->dev, "Unable to add EDAC device: %d\n", err);
		return err;
	}

	dev_info(&pdev->dev, "CPS FuSa fault monitoring enabled\n");
	return 0;
}

static const struct of_device_id mips_cpc_fault_of_match[] = {
	{ .compatible = "mti,mips-cpc-fault-status" },
	{},
};
MODULE_DEVICE_TABLE(of, mips_cpc_fault_of_match);

static struct platform_driver mips_cpc_fault_driver = {
	.probe = mips_cpc_fault_probe,
	.driver = {
		.name = "mips-cps-fault-status",
		.of_match_table = mips_cpc_fault_of_match,
	},
};
module_platform_driver(mips_cpc_fault_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer");
MODULE_DESCRIPTION("EDAC Driver for Altera Memories");
