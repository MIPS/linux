/*
 * Copyright (C) 2017 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
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

#include <asm/cpu-type.h>
#include <asm/mips-cm.h>
#include <asm/mipsregs.h>

#define read_c0_saari()		__read_ulong_c0_register($9, 6)
#define write_c0_saari(val)	__write_ulong_c0_register($9, 6, val)

#define read_c0_saar()		__read_ulong_c0_register($9, 7)
#define write_c0_saar(val)	__write_ulong_c0_register($9, 7, val)

#define MIPS_SAAR_ENABLE	BIT(0)
#define MIPS_SAAR_SIZE_SHIFT	1
#define MIPS_SAAR_SIZE		GENMASK(5, MIPS_SAAR_SIZE_SHIFT)

#define SAAR_MAX_COUNT		3

static unsigned long saar_regs[SAAR_MAX_COUNT];
static DECLARE_BITMAP(saar_regs_used, SAAR_MAX_COUNT);

struct saar_device {
	struct device *dev;
	struct bin_attribute battr_name;
	struct bin_attribute battr_map;
	phys_addr_t base_phys;
};

static ssize_t saar_name_read(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *attr,
			      char *buf, loff_t pos, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	const char *name = dev_get_platdata(dev);

	memcpy(buf, &name[pos], count);

	return count;
}

static int saar_mmap(struct file *file, struct kobject *kobj,
		     struct bin_attribute *battr, struct vm_area_struct *vma)
{
	struct device *dev = kobj_to_dev(kobj);
	struct saar_device *sdev = dev_get_drvdata(dev);

	vma->vm_pgoff += sdev->base_phys >> PAGE_SHIFT;

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start,
			    vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static int saar_probe(struct platform_device *pdev)
{
	struct saar_device *sdev;
	struct resource *res;
	const char *name;
	int err;

	sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->dev = &pdev->dev;
	dev_set_drvdata(sdev->dev, sdev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(sdev->dev, "found no memory resource\n");
		return -EINVAL;
	}

	if (!devm_request_mem_region(sdev->dev, res->start,
				     resource_size(res), pdev->name)) {
		dev_err(sdev->dev, "could not request region for resource\n");
		return -EBUSY;
	}

	sdev->base_phys = res->start;
	name = dev_get_platdata(sdev->dev);

	sysfs_bin_attr_init(&sdev->battr_name);
	sdev->battr_name.attr.name = "name";
	sdev->battr_name.attr.mode = S_IRUSR;
	sdev->battr_name.read = saar_name_read;
	sdev->battr_name.size = strlen(name);

	err = device_create_bin_file(sdev->dev, &sdev->battr_name);
	if (err)
		return err;

	sysfs_bin_attr_init(&sdev->battr_map);
	sdev->battr_map.attr.name = "map";
	sdev->battr_map.attr.mode = S_IRUSR | S_IWUSR;
	sdev->battr_map.mmap = saar_mmap;
	sdev->battr_map.size = resource_size(res);

	err = device_create_bin_file(sdev->dev, &sdev->battr_map);
	if (err)
		return err;

	dev_info(sdev->dev, "%zu KiB %s @ %pa\n",
		 sdev->battr_map.size / 1024, name, &sdev->base_phys);

	return 0;
}

static struct platform_driver saar_driver = {
	.driver = {
		.name = "saar-device",
	},
	.probe = saar_probe,
};

static int __init saar_register_dev(unsigned int idx, const char *name)
{
	static phys_addr_t phys_alloc = 0x17000000;
	struct platform_device *pdev;
	phys_addr_t phys_base;
	struct resource res;
	size_t sz;

	write_c0_saari(idx);
	if (read_c0_saari() != idx)
		return -ENODEV;

	sz = 1 << ((read_c0_saar() & MIPS_SAAR_SIZE) >> MIPS_SAAR_SIZE_SHIFT);
	if (!sz)
		return -ENODEV;

	phys_base = phys_alloc;
	saar_regs[idx] = (phys_base >> 4) | MIPS_SAAR_ENABLE;
	write_c0_saar(saar_regs[idx]);
	if (!(read_c0_saar() & MIPS_SAAR_ENABLE))
		return -ENODEV;

	phys_alloc = ALIGN(phys_alloc + sz, max_t(ulong, BIT(16), PAGE_SIZE));

	memset(&res, 0, sizeof(res));
	res.flags = IORESOURCE_MEM;
	res.start = phys_base;
	res.end = phys_base + sz - 1;

	pdev = platform_device_register_resndata(NULL, saar_driver.driver.name,
						 idx, &res, 1,
						 name, strlen(name) + 1);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	set_bit(idx, saar_regs_used);
	return 0;
}

static int saar_cpu_online(unsigned int cpu)
{
	unsigned int i;

	for_each_set_bit(i, saar_regs_used, SAAR_MAX_COUNT) {
		write_c0_saari(i);
		if (read_c0_saari() != i)
			continue;

		write_c0_saar(saar_regs[i]);
	}

	return 0;
}

static int __init saar_init(void)
{
	int err;

	err = platform_driver_register(&saar_driver);
	if (err)
		return err;

	switch (boot_cpu_type()) {
	case CPU_I6500:
		/*
		 * Some I6500 bitfiles allow the ITU to be configured via SAAR
		 * even though one isn't present. We check for ITU presence
		 * here to ensure that we don't later attempt to access a
		 * device which doesn't exist.
		 */
		if (mips_cm_present() && read_gcr_config() & BIT(31)) {
			err = saar_register_dev(0, "itu");
			if (err && (err != -ENODEV))
				return err;
		}

		err = saar_register_dev(1, "dspram");
		if (err && (err != -ENODEV))
			return err;

		err = saar_register_dev(2, "ispram");
		if (err && (err != -ENODEV))
			return err;
		break;
	}

	return cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				 "misc/mips-saar:online",
				 saar_cpu_online, NULL);
}
postcore_initcall(saar_init);
