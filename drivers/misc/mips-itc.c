/*
 * Copyright (C) 2018 MIPS Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/cpuhotplug.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/platform_device.h>

#include <asm/cacheops.h>
#include <asm/cpu-type.h>
#include <asm/mipsregs.h>

#define ERRCTL_ITC BIT(26)

#define GEN_ITC_ACCESSORS(off, name)				\
static inline uint32_t read_itc_##name(void)			\
{								\
	uint32_t val, ecc = read_c0_ecc();			\
	write_c0_ecc(ecc | ERRCTL_ITC);				\
	back_to_back_c0_hazard();				\
	asm volatile("cache\t%0, %1($zero)"			\
		     : /* no outputs */				\
		     : "i"(Index_Load_Tag_D),			\
		       "i"(off));				\
	back_to_back_c0_hazard();				\
	val = read_c0_dtaglo();					\
	write_c0_ecc(ecc);					\
	back_to_back_c0_hazard();				\
	return val;						\
}								\
								\
static inline void write_itc_##name(uint32_t val)		\
{								\
	uint32_t ecc = read_c0_ecc();				\
	write_c0_ecc(ecc | ERRCTL_ITC);				\
	write_c0_dtaglo(val);					\
	back_to_back_c0_hazard();				\
	asm volatile("cache\t%0, %1($zero)"			\
		     : /* no outputs */				\
		     : "i"(Index_Store_Tag_D),			\
		       "i"(off));				\
	write_c0_ecc(ecc);					\
	back_to_back_c0_hazard();				\
}

GEN_ITC_ACCESSORS(0x0, addrmap0)
GEN_ITC_ACCESSORS(0x8, addrmap1)

static unsigned int itc_num_cells(void)
{
	return (read_itc_addrmap1() >> 20) & 0x7ff;
}

struct itc_device {
	struct device *dev;
	struct bin_attribute battr_map;
	struct bin_attribute battr_cells;
	phys_addr_t base_phys;
	char str_cells[16];
};

static uint32_t itc_addr[2];

static int itc_mmap(struct file *file, struct kobject *kobj,
		    struct bin_attribute *battr, struct vm_area_struct *vma)
{
	struct device *dev = kobj_to_dev(kobj);
	struct itc_device *itc = dev_get_drvdata(dev);

	vma->vm_pgoff += itc->base_phys >> PAGE_SHIFT;

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start,
			    pgprot_noncached(vma->vm_page_prot)))
		return -EAGAIN;

	return 0;
}

static ssize_t itc_cells_read(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *attr,
			      char *buf, loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct itc_device *itc = dev_get_drvdata(dev);

	memcpy(buf, &itc->str_cells[pos], count);

	return count;
}

static int itc_probe(struct platform_device *pdev)
{
	struct itc_device *itc;
	struct resource *res;
	uint32_t num_cells;
	int err;

	num_cells = itc_num_cells();
	if (!num_cells)
		return -ENODEV;

	itc = devm_kzalloc(&pdev->dev, sizeof(*itc), GFP_KERNEL);
	if (!itc)
		return -ENOMEM;

	itc->dev = &pdev->dev;
	dev_set_drvdata(itc->dev, itc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(itc->dev, "found no memory resource\n");
		return -EINVAL;
	}

	if (!devm_request_mem_region(itc->dev, res->start,
				     resource_size(res), pdev->name)) {
		dev_err(itc->dev, "could not request region for resource\n");
		return -EBUSY;
	}

	itc->base_phys = res->start;
	snprintf(itc->str_cells, sizeof(itc->str_cells),
		 "%u", num_cells);

	sysfs_bin_attr_init(&itc->battr_map);
	itc->battr_map.attr.name = "map";
	itc->battr_map.attr.mode = S_IRUSR | S_IWUSR;
	itc->battr_map.mmap = itc_mmap;
	itc->battr_map.size = resource_size(res);

	err = device_create_bin_file(itc->dev, &itc->battr_map);
	if (err)
		return err;

	sysfs_bin_attr_init(&itc->battr_cells);
	itc->battr_cells.attr.name = "cells";
	itc->battr_cells.attr.mode = S_IRUSR;
	itc->battr_cells.read = itc_cells_read;
	itc->battr_cells.size = strlen(itc->str_cells);

	err = device_create_bin_file(itc->dev, &itc->battr_cells);
	if (err)
		return err;

	dev_info(itc->dev, "%u cells\n", num_cells);
	return 0;
}

static struct platform_driver itc_driver = {
	.driver = {
		.name = "mips-itc",
	},
	.probe = itc_probe,
};

static int itc_cpu_online_cache_tags(unsigned int cpu)
{
	write_itc_addrmap1(itc_addr[1]);
	write_itc_addrmap0(itc_addr[0]);
	return 0;
}

static int __init itc_register_cache_tags(void)
{
	const phys_addr_t phys_addr = 0x17000000;
	struct platform_device *pdev;
	struct resource res;
	int err;

	itc_addr[0] = phys_addr | BIT(0);
	itc_addr[1] = ~PAGE_MASK & GENMASK(16, 10);

	err = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"misc/mips-itc:online",
				itc_cpu_online_cache_tags, NULL);
	if (err < 0)
		return err;

	memset(&res, 0, sizeof(res));
	res.flags = IORESOURCE_MEM;
	res.start = phys_addr;
	res.end = phys_addr + PAGE_SIZE - 1;

	pdev = platform_device_register_resndata(NULL, itc_driver.driver.name,
						 0, &res, 1,
						 "itc", 4);
	return PTR_ERR_OR_ZERO(pdev);
}

static int __init itc_init(void)
{
	int err;

	err = platform_driver_register(&itc_driver);
	if (err)
		return err;

	switch (boot_cpu_type()) {
	case CPU_I7200:
		return itc_register_cache_tags();

	default:
		return 0;
	}
}
postcore_initcall(itc_init);
