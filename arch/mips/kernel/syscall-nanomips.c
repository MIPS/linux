/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Imagination Technologies Ltd.
 */

#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <asm/sim.h>
#include <asm/syscalls.h>

/*
 * clone needs to save callee saved registers so they are copied correctly to
 * the child process context.
 */
asmlinkage long __sys_clone(unsigned long, unsigned long, int __user *,
			    unsigned long, int __user *);
save_static_function(sys_clone);
#define sys_clone __sys_clone

SYSCALL_DEFINE1(set_thread_area, unsigned long, addr)
{
	struct thread_info *ti = task_thread_info(current);

	ti->tp_value = addr;
	if (cpu_has_userlocal)
		write_c0_userlocal(addr);

	return 0;
}

/* Provide the actual syscall number to call mapping. */
#undef __SYSCALL
#define __SYSCALL(nr, call) [nr] = (call),

/*
 * Note that we can't include <linux/unistd.h> here since the header
 * guard will defeat us; <asm/unistd.h> checks for __SYSCALL as well.
 */
const void *sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls-1] = sys_ni_syscall,
#include <asm/unistd.h>
};
