/*
 * MIPS I7200 SRAM Driver
 *
 * Copyright (C) 2018 MIPS Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/cpuhotplug.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/memblock.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include <asm/cacheops.h>
#include <asm/cpu-type.h>
#include <asm/hazards.h>
#include <asm/mipsregs.h>

#define ERRCTL_SPR			BIT(28)

#define read_c0_sram_ctl()		__read_32bit_c0_register($22, 3)
#define write_c0_sram_ctl(val)		__write_32bit_c0_register($22, 3, val)
__BUILD_SET_C0(sram_ctl)

#define SRAM_CTL_DSP_EN			BIT(0)
#define SRAM_CTL_ISP_EN			BIT(1)
#define SRAM_CTL_USP_EN			BIT(2)
#define SRAM_CTL_DSPPB_EN		BIT(4)
#define SRAM_CTL_USPDPB_DIS		BIT(5)
#define SRAM_CTL_USPIPB_DIS		BIT(6)
#define SRAM_CTL_ISPPB_DIS		BIT(7)

#define write_c0_idatalo(val)		__write_32bit_c0_register($28, 1, val)
#define write_c0_idatahi(val)		__write_32bit_c0_register($29, 1, val)

struct sram {
	struct miscdevice misc;

	phys_addr_t base;
	phys_addr_t size;

	unsigned int enable_bit;

	bool (*detect)(struct sram *s);
};

static struct sram srams[];
static u32 sram_ctl;
static bool nodsppb, nouspdpb, nouspipb, noisppb;

static unsigned long spram_get_unmapped_area(struct file *file,
					     unsigned long addr,
					     unsigned long len,
					     unsigned long pgoff,
					     unsigned long flags)
{
	struct miscdevice *misc = file->private_data;
	struct sram *s = container_of(misc, struct sram, misc);
	unsigned long off, off_end, off_align, len_align, addr_align;

	off = pgoff << PAGE_SHIFT;
	off_end = off + len;
	off_align = round_up(off, s->size);

	if ((off_end <= off_align) || ((off_end - off_align) < s->size))
		goto fallback;

	len_align = len + s->size;
	if ((off + len_align) < off)
		goto fallback;

	addr_align = current->mm->get_unmapped_area(file, addr, len_align,
						    pgoff, flags);
	if (!IS_ERR_VALUE(addr_align)) {
		addr_align += (off - addr_align) & (s->size - 1);
		return addr_align;
	}

fallback:
	WARN(1, "Unable to guarantee SPRAM virtual alignment\n");
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

static loff_t spram_llseek(struct file *file, loff_t offset, int orig)
{
	struct miscdevice *misc = file->private_data;
	struct sram *s = container_of(misc, struct sram, misc);

	return fixed_size_llseek(file, offset, orig, s->size);
}

static int spram_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct miscdevice *misc = file->private_data;
	struct sram *s = container_of(misc, struct sram, misc);

	vma->vm_pgoff += s->base >> PAGE_SHIFT;

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start,
			    pgprot_noncached(vma->vm_page_prot)))
		return -EAGAIN;

	return 0;
}

static ssize_t spram_write(struct file *file, const char __user *buf,
			   size_t size, loff_t *ppos)
{
	struct miscdevice *misc = file->private_data;
	struct sram *s = container_of(misc, struct sram, misc);
	void __iomem *virt;
	int err;

	virt = ioremap_uc(s->base, s->size);
	err = copy_from_user(virt + *ppos, buf, size);
	iounmap(virt);
	if (err)
		return -EFAULT;
	*ppos += size;
	return size;
}

static ssize_t ispram_write(struct file *file, const char __user *buf,
			    size_t size, loff_t *ppos)
{
	struct miscdevice *misc = file->private_data;
	struct sram *s = container_of(misc, struct sram, misc);
	unsigned long flags, off, paddr;
	u32 ctl;
	union {
		struct {
			u32 lo;
			u32 hi;
		};
		char ch[8];
	} data;

	for (off = *ppos; off < ((*ppos + size + 7) & ~7); off += 8, buf += 8) {
		if (copy_from_user(data.ch, buf, 8))
			return -EFAULT;

		paddr = s->base + off;

		local_irq_save(flags);
		ctl = read_c0_ecc();
		write_c0_ecc(ctl | ERRCTL_SPR);
		back_to_back_c0_hazard();
		write_c0_idatalo(data.lo);
		write_c0_idatahi(data.hi);
		back_to_back_c0_hazard();
		__builtin_mips_cache(Cache_I | (0x3 << 2), (void *)paddr);
		back_to_back_c0_hazard();
		write_c0_ecc(ctl);
		back_to_back_c0_hazard();
		local_irq_restore(flags);
	}

	mb();
	instruction_hazard();
	*ppos += size;
	return size;
}

static const struct file_operations ispram_fops = {
	.owner		= THIS_MODULE,
	.get_unmapped_area = spram_get_unmapped_area,
	.llseek		= spram_llseek,
	.mmap		= spram_mmap,
	.write		= ispram_write,
};

static const struct file_operations duspram_fops = {
	.owner		= THIS_MODULE,
	.get_unmapped_area = spram_get_unmapped_area,
	.llseek		= spram_llseek,
	.mmap		= spram_mmap,
	.write		= spram_write,
};

static bool uspram_detect(struct sram *s)
{
	bool have_uspram;
	u32 ctl;

	/*
	 * Try to figure out if we have USPRAM by enabling it & seeing if the
	 * enable bit sticks. This is potentially disruptive if we happen to be
	 * using the memory at its address, but unfortunately there's no Config
	 * bit like there is for DSPRAM & ISPRAM...
	 */
	ctl = set_c0_sram_ctl(SRAM_CTL_USP_EN);
	back_to_back_c0_hazard();
	have_uspram = !!(read_c0_sram_ctl() & SRAM_CTL_USP_EN);
	write_c0_sram_ctl(ctl);
	if (!have_uspram)
		return false;

	/*
	 * Really... an undiscoverable & unchangeable address range that can
	 * differ based on core configuration... Come on hardware folk..!
	 *
	 * These values are correct for the MTK_Tapeout configs as of
	 * changelist 4934677.
	 */
	s->base = 0x17800000;
	s->size = SZ_256K;

	return true;
}

static bool ispram_detect(struct sram *s)
{
	u32 ctl, tag0, tag1;

	if (!(read_c0_config() & BIT(24)))
		return false;

	ctl = read_c0_ecc();
	write_c0_ecc(ctl | ERRCTL_SPR);
	back_to_back_c0_hazard();
	asm volatile("cache\t%0, 0($zero)" :: "i"(Index_Load_Tag_I));
	back_to_back_c0_hazard();
	tag0 = read_c0_taglo();
	back_to_back_c0_hazard();
	asm volatile("cache\t%0, 8($zero)" :: "i"(Index_Load_Tag_I));
	back_to_back_c0_hazard();
	tag1 = read_c0_taglo();
	back_to_back_c0_hazard();
	write_c0_ecc(ctl);
	back_to_back_c0_hazard();

	s->base = tag0 & GENMASK(31, 12);
	s->size = tag1 & GENMASK(19, 12);

	return !!s->size;
}

static bool dspram_detect(struct sram *s)
{
	u32 ctl, tag0;

	if (!(read_c0_config() & BIT(23)))
		return false;

	ctl = read_c0_ecc();
	write_c0_ecc(ctl | ERRCTL_SPR);
	back_to_back_c0_hazard();
	asm volatile("cache\t%0, 0($zero)" :: "i"(Index_Load_Tag_D));
	back_to_back_c0_hazard();
	tag0 = read_c0_dtaglo();
	back_to_back_c0_hazard();
	write_c0_ecc(ctl);
	back_to_back_c0_hazard();

	s->base = tag0 & GENMASK(31, 12);

	/*
	 * The DSPRAM size tag isn't implemented... Apparently it isn't meant
	 * to be, and neither is the ISPRAM one or the address tags, but the
	 * replacement (likely registers in CDMM) isn't implemented either so
	 * we don't have anything better yet... Eww!
	 *
	 * For now we use the tags that are implemented despite them not being
	 * the approved way of discovering SPRAMs, because they're all we have.
	 * We presume the DSPRAM is the same size as the ISPRAM because we have
	 * no better data available...
	 *
	 * See SBM 84953 for details.
	 */
	s->size = srams[1].size;

	return !!s->size;
}

static struct sram srams[] = {
	{
		.misc = {
			.name = "uspram",
			.minor = MISC_DYNAMIC_MINOR,
			.mode = S_IRUSR | S_IWUSR | S_IXUSR,
			.fops = &duspram_fops,
		},
		.detect = uspram_detect,
		.enable_bit = SRAM_CTL_USP_EN,
	},
	{
		.misc = {
			.name = "ispram",
			.minor = MISC_DYNAMIC_MINOR,
			.mode = S_IRUSR | S_IWUSR | S_IXUSR,
			.fops = &ispram_fops,
		},
		.detect = ispram_detect,
		.enable_bit = SRAM_CTL_ISP_EN,
	},
	{
		.misc = {
			.name = "dspram",
			.minor = MISC_DYNAMIC_MINOR,
			.mode = S_IRUSR | S_IWUSR,
			.fops = &duspram_fops,
		},
		.detect = dspram_detect,
		.enable_bit = SRAM_CTL_DSP_EN,
	},
};

static int spram_cpu_online(unsigned int cpu)
{
	write_c0_sram_ctl(sram_ctl);
	return 0;
}

static int __init spram_init(void)
{
	int err, i;

	/* This is very I7200-specific */
	if (boot_cpu_type() != CPU_I7200)
		return -ENODEV;

	sram_ctl = read_c0_sram_ctl();
	sram_ctl &= ~SRAM_CTL_DSP_EN;
	sram_ctl &= ~SRAM_CTL_ISP_EN;
	sram_ctl &= ~SRAM_CTL_USP_EN;

	if (nodsppb) {
		pr_info("Disabling DSPPB (DSPRAM predictor)\n");
		sram_ctl &= ~SRAM_CTL_DSPPB_EN;
	}
	if (nouspdpb) {
		pr_info("Disabling USPDPB (USPRAM D-side predictor)\n");
		sram_ctl |= SRAM_CTL_USPDPB_DIS;
	}
	if (nouspipb) {
		pr_info("Disabling USPIPB (USPRAM I-side predictor)\n");
		sram_ctl |= SRAM_CTL_USPIPB_DIS;
	}
	if (noisppb) {
		pr_info("Disabling ISPPB (ISPRAM predictor)\n");
		sram_ctl |= SRAM_CTL_ISPPB_DIS;
	}

	for (i = 0; i < ARRAY_SIZE(srams); i++) {
		pr_info("%cSPRAM:", toupper(srams[i].misc.name[0]));

		if (!srams[i].detect(&srams[i])) {
			pr_cont(" None\n");
			continue;
		}

		if (memblock_is_memory(srams[i].base) ||
		    memblock_is_memory(srams[i].base + srams[i].size - 1)) {
			pr_cont(" Overlaps DDR, Ignoring\n");
			continue;
		}

		pr_cont(" %uKB @ %pa\n",
			(unsigned int)(srams[i].size / SZ_1K),
			&srams[i].base);

		err = misc_register(&srams[i].misc);
		if (err) {
			pr_err("Failed to register %cSPRAM device: %d\n",
			       toupper(srams[i].misc.name[0]), err);
			continue;
		}

		sram_ctl |= srams[i].enable_bit;
	}

	err = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"misc/mips-i7200-spram:online",
				spram_cpu_online, NULL);
	if (err < 0)
		return err;

	return 0;
}
device_initcall(spram_init);

#define GEN_ARG_PARSE(name)			\
static int __init parse_##name(char *arg)	\
{						\
	name = true;				\
	return 0;				\
}						\
early_param(#name, parse_##name);

GEN_ARG_PARSE(nodsppb)
GEN_ARG_PARSE(nouspdpb)
GEN_ARG_PARSE(nouspipb)
GEN_ARG_PARSE(noisppb)
