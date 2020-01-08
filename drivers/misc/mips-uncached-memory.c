/*
 * Copyright (C) 2018 MIPS Technologies
 * Author: Matt Redfearn <matt.redfearn@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define pr_fmt(fmt) "MIPS uncached memory: " fmt

#include <linux/cpuhotplug.h>
#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/mm.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <asm/bcache.h>
#include <asm/cacheflush.h>
#include <asm/cacheops.h>
#include <asm/cpu-type.h>
#include <asm/mipsregs.h>
#include <asm/r4kcache.h>
#include <asm/tlbdebug.h>

#define BOSTON_BUILD_CONFIG0 		(0x34)
#define BOSTON_BUILD_CONFIG0_LLSC	(BIT(25))

struct mum_device {
	struct device *dev;
	struct bin_attribute battr_map;
	void *memory;
};

static int mum_mmap(struct file *file, struct kobject *kobj,
		    struct bin_attribute *battr, struct vm_area_struct *vma)
{
	struct device *dev = kobj_to_dev(kobj);
	struct mum_device *mum = dev_get_drvdata(dev);
	unsigned long size = vma->vm_end - vma->vm_start;

	pr_debug("mmap %ld bytes uncached from physical %lx (%px kern, %lx userspace)\n",
		 size, virt_to_phys(mum->memory), mum->memory, vma->vm_start);

	vma->vm_pgoff += virt_to_phys(mum->memory) >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start,
			    vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static int mum_probe(struct platform_device *pdev)
{
	struct mum_device *mum;

	mum = devm_kzalloc(&pdev->dev, sizeof(*mum), GFP_KERNEL);
	if (!mum)
		return -ENOMEM;

	mum->dev = &pdev->dev;
	dev_set_drvdata(mum->dev, mum);

	/* 1 page by default */
	mum->memory = (void*)__get_free_page(GFP_KERNEL);
	if (!mum->memory)
		return -ENOMEM;

	/* Ensure kernel page is written back. */
	preempt_disable();
	memset(mum->memory, 0, PAGE_SIZE);
	blast_dcache_range((unsigned long)mum->memory,
			   (unsigned long)mum->memory + PAGE_SIZE);
	bc_wback_inv((unsigned long)mum->memory, PAGE_SIZE);
	__sync();
	preempt_enable();

	sysfs_bin_attr_init(&mum->battr_map);
	mum->battr_map.attr.name = "map";
	mum->battr_map.attr.mode = S_IRUSR | S_IWUSR;
	mum->battr_map.mmap = mum_mmap;
	mum->battr_map.size = PAGE_SIZE;

	return device_create_bin_file(mum->dev, &mum->battr_map);
}

static struct platform_driver mum_driver = {
	.driver = {
		.name = "mips-uncached-memory",
	},
	.probe = mum_probe,
};

static struct platform_device mum_device = {
	.name = "mips-uncached-memory",
};

static int __init mum_init(void)
{
	struct regmap *plt_regs;
	u32 reg;
	int err;

	err = platform_driver_register(&mum_driver);
	if (err)
		return err;

	plt_regs = syscon_regmap_lookup_by_compatible("img,boston-platform-regs");
	if (IS_ERR(plt_regs)) {
		/* Not Boston? */
		return 0;
	}

	regmap_read(plt_regs, BOSTON_BUILD_CONFIG0, &reg);

	if ((read_c0_config5() & MIPS_CONF5_ULS) &&
	    (reg & BOSTON_BUILD_CONFIG0_LLSC)) {
		pr_info("Supported on this platform\n");
		return platform_device_register(&mum_device);
	}

	return 0;
}
postcore_initcall(mum_init);
