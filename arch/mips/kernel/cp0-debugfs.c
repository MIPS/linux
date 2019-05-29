/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <asm/cacheflush.h>
#include <asm/uasm.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>

struct cpu_cp0_state {
	int cpu;

	unsigned reg;
	unsigned sel;

	u32 code[3];
};

struct cp0_access_state {
	struct cpu_cp0_state *cp0_state;
	unsigned long val;
	void (*uasm_access)(u32 **buf, unsigned a, unsigned b, unsigned c);
	bool is_write;
};

static DEFINE_PER_CPU_ALIGNED(struct cpu_cp0_state, cp0_state);
extern struct dentry *mips_debugfs_dir;

static void print_warning(void)
{
	static bool warned = false;

	if (warned)
		return;

	pr_warn("By making use of cp0 debugfs access you may easily "
		"break the system. Please be careful, and be sure any "
		"bugs you see from now onwards are not caused by "
		"your own actions. Do not rely upon this debugfs "
		"access from programs - it exists in our engineering "
		"kernels only for debug purposes only, and could be "
		"removed at any time.\n");

	add_taint(TAINT_USER, LOCKDEP_STILL_OK);
	warned = true;
}

static ssize_t cp0_reg_read(struct file *file, char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	struct cpu_cp0_state *state = file->private_data;
	char buf[16];
	int len;

	len = snprintf(buf, sizeof(buf), "%d.%d\n",
		       state->reg, state->sel);
	BUG_ON(len >= sizeof(buf));

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t cp0_reg_write(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct cpu_cp0_state *state = file->private_data;
	char buf[32], *dot;
	ssize_t buf_size;
	unsigned long reg, sel;
	int err;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';

	dot = strchr(buf, '.');
	if (dot) {
		err = kstrtoul(dot + 1, 10, &sel);
		if (err)
			return -EINVAL;
		*dot = '\0';
	} else {
		sel = 0;
	}

	err = kstrtoul(buf, 10, &reg);
	if (err)
		return -EINVAL;

	/* ensure the values fit inside an instruction */
	if (reg > 0x1f || sel > 0x7)
		return -EINVAL;

	state->reg = reg;
	state->sel = sel;

	return count;
}

static const struct file_operations cp0_reg_fops = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = cp0_reg_read,
	.write = cp0_reg_write,
};

static void local_cp0_access(void *arg)
{
	struct cp0_access_state *state = arg;
	unsigned reg = state->cp0_state->reg;
	unsigned sel = state->cp0_state->sel;
	ulong (*read_fn)(void);
	void (*write_fn)(ulong val);
	u32 *p, *code;

	BUG_ON(state->cp0_state != this_cpu_ptr(&cp0_state));
	print_warning();

	/* Generate a function that performs the CP0 access and then returns */
	p = code = state->cp0_state->code;
	state->uasm_access(&p, state->is_write ? 4 : 2, reg, sel);
	uasm_i_jalr(&p, 0, 31);
	uasm_i_nop(&p);
	flush_icache_range((ulong)code, (ulong)p);

	/* Perform the coprocessor access */
	if (state->is_write) {
		write_fn = (void *)code;
		write_fn(state->val);
	} else {
		read_fn = (void *)code;
		state->val = read_fn();
	}
}

#define GEN_CP0_FOPS(suffix, bits, read_uasm_fn, write_uasm_fn)				\
static ssize_t cp0_data##suffix##_read(struct file *file, char __user *user_buf,	\
				     size_t count, loff_t *ppos)			\
{											\
	struct cp0_access_state state = {						\
		.cp0_state = file->private_data,					\
		.uasm_access = read_uasm_fn,						\
		.is_write = false,							\
	};										\
	int cpu = state.cp0_state->cpu;							\
	char buf[(bits / 4) + 2];							\
	int len, err;									\
											\
	err = smp_call_function_single(cpu, local_cp0_access,				\
				       &state, 1);					\
	if (err)									\
		return -EINVAL;								\
											\
	len = snprintf(buf, sizeof(buf), "%0*llx\n", bits / 4, (u64)(u##bits)state.val);\
	BUG_ON(len != sizeof(buf) - 1);							\
											\
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);		\
}											\
											\
static ssize_t cp0_data##suffix##_write(struct file *file, const char __user *user_buf,	\
				      size_t count, loff_t *ppos)			\
{											\
	struct cp0_access_state state = {						\
		.cp0_state = file->private_data,					\
		.uasm_access = write_uasm_fn,						\
		.is_write = true,							\
	};										\
	int cpu = state.cp0_state->cpu;							\
	char buf[32];									\
	ssize_t buf_size;								\
	int err;									\
											\
	buf_size = min(count, sizeof(buf) - 1);						\
	if (copy_from_user(buf, user_buf, buf_size))					\
		return -EFAULT;								\
											\
	buf[buf_size] = '\0';								\
											\
	err = kstrtoul(buf, 16, &state.val);						\
	if (err)									\
		return -EINVAL;								\
											\
	err = smp_call_function_single(cpu, local_cp0_access,				\
				       &state, 1);					\
	if (err)									\
		return -EINVAL;								\
											\
	return count;									\
}											\
											\
static const struct file_operations cp0_data##suffix##_fops = {				\
	.open = simple_open,								\
	.llseek = default_llseek,							\
	.read = cp0_data##suffix##_read,						\
	.write = cp0_data##suffix##_write,						\
};

GEN_CP0_FOPS(32, 32, uasm_i_mfc0, uasm_i_mtc0)
GEN_CP0_FOPS(32h, 32, uasm_i_mfhc0, uasm_i_mthc0)
GEN_CP0_FOPS(64, 64, uasm_i_dmfc0, uasm_i_dmtc0)

static int __init cp0_debugfs_init(void)
{
	struct dentry *dir, *file;
	char dir_name[16];
	int cpu, len;

	if (!mips_debugfs_dir)
		return -ENODEV;

	for_each_possible_cpu(cpu) {
		per_cpu_ptr(&cp0_state, cpu)->cpu = cpu;

		len = snprintf(dir_name, sizeof(dir_name), "cp0-%d", cpu);
		if (len >= sizeof(dir_name))
			return -EINVAL;

		dir = debugfs_create_dir(dir_name, mips_debugfs_dir);
		if (IS_ERR(dir))
			return PTR_ERR(dir);

		file = debugfs_create_file("reg", S_IRUGO | S_IWUSR, dir,
					   per_cpu_ptr(&cp0_state, cpu),
					   &cp0_reg_fops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		file = debugfs_create_file("data32", S_IRUGO | S_IWUSR, dir,
					   per_cpu_ptr(&cp0_state, cpu),
					   &cp0_data32_fops);
		if (IS_ERR(file))
			return PTR_ERR(file);

		if (cpu_has_xpa) {
			file = debugfs_create_file("data32h", S_IRUGO | S_IWUSR, dir,
						   per_cpu_ptr(&cp0_state, cpu),
						   &cp0_data32h_fops);
			if (IS_ERR(file))
				return PTR_ERR(file);
		}

		if (cpu_has_64bits) {
			file = debugfs_create_file("data64", S_IRUGO | S_IWUSR, dir,
						   per_cpu_ptr(&cp0_state, cpu),
						   &cp0_data64_fops);
			if (IS_ERR(file))
				return PTR_ERR(file);
		}
	}

	return 0;
}
late_initcall(cp0_debugfs_init);
