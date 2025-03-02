/*
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * Copyright (C) 2000 - 2001 by Kanoj Sarcar (kanoj@sgi.com)
 * Copyright (C) 2000 - 2001 by Silicon Graphics, Inc.
 * Copyright (C) 2000, 2001, 2002 Ralf Baechle
 * Copyright (C) 2000, 2001 Broadcom Corporation
 */
#ifndef __ASM_SMP_H
#define __ASM_SMP_H

#include <linux/bitops.h>
#include <linux/linkage.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/cpumask.h>

#include <linux/atomic.h>
#include <asm/smp-ops.h>
#include <asm/thread_info.h>

extern int smp_num_siblings;
extern cpumask_t cpu_sibling_map[];
extern cpumask_t cpu_core_map[];
extern cpumask_t cpu_foreign_map[];

static inline int raw_smp_processor_id(void)
{
#if defined(__VDSO__)
	extern int vdso_smp_processor_id(void)
		__compiletime_error("VDSO should not call smp_processor_id()");
	return vdso_smp_processor_id();
#elif defined(CONFIG_MIPS_PGD_C0_CONTEXT)
	return read_const_c0_xcontext() >> SMP_CPUID_REGSHIFT;
#else
	return read_const_c0_context() >> SMP_CPUID_REGSHIFT;
#endif
}
#define raw_smp_processor_id raw_smp_processor_id

/* Map from cpu id to sequential logical cpu number.  This will only
   not be idempotent when cpus failed to come on-line.	*/
extern int __cpu_number_map[CONFIG_MIPS_NR_CPU_NR_MAP];
#define cpu_number_map(cpu)  __cpu_number_map[cpu]

/* The reverse map from sequential logical cpu number to cpu id.  */
extern int __cpu_logical_map[NR_CPUS];
#define cpu_logical_map(cpu)  __cpu_logical_map[cpu]

#define NO_PROC_ID	(-1)

enum ipi_action {
	/*
	 * Used to request that a remote CPU should call scheduler_ipi() in
	 * order to reschedule.
	 */
	_SMP_RESCHEDULE_YOURSELF,
# define SMP_RESCHEDULE_YOURSELF	BIT(_SMP_RESCHEDULE_YOURSELF)

	/*
	 * Used to request that a remote CPU calls a function specified by the
	 * CPU which sent the IPI.
	 */
	_SMP_CALL_FUNCTION,
# define SMP_CALL_FUNCTION		BIT(_SMP_CALL_FUNCTION)

#ifdef CONFIG_CPU_CAVIUM_OCTEON
	/*
	 * Used by Cavium Octeon systems to request that a remote CPU flushes
	 * its icache.
	 */
	_SMP_ICACHE_FLUSH,
# define SMP_ICACHE_FLUSH		BIT(_SMP_ICACHE_FLUSH)
#else
# define SMP_ICACHE_FLUSH		0
#endif

#ifdef CONFIG_MACH_LOONGSON64
	/*
	 * Used by Loongson64 secondary CPUs to ask core 0 for its current cop0
	 * Count value which is used to approximately synchronise the Count
	 * value on the secondaries.
	 */
	_SMP_ASK_C0COUNT,
# define SMP_ASK_C0COUNT		BIT(_SMP_ASK_C0COUNT)
#else
# define SMP_ASK_C0COUNT		0
#endif

#ifdef CONFIG_SMP_SINGLE_IPI
	/*
	 * Used to implement arch_trigger_cpumask_backtrace(), which cannot use
	 * SMP_CALL_FUNCTION because it may be invoked in IRQ context.
	 */
	_SMP_BACKTRACE,
# define SMP_BACKTRACE			BIT(_SMP_BACKTRACE)
#else
# define SMP_BACKTRACE			0
#endif
};

/* Mask of CPUs which are currently definitely operating coherently */
extern cpumask_t cpu_coherent_mask;

extern asmlinkage void smp_bootstrap(void);

extern void calculate_cpu_foreign_map(void);

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */
static inline void smp_send_reschedule(int cpu)
{
	extern const struct plat_smp_ops *mp_ops;	/* private */

	mp_ops->send_ipi_single(cpu, SMP_RESCHEDULE_YOURSELF);
}

#ifdef CONFIG_HOTPLUG_CPU
static inline int __cpu_disable(void)
{
	extern const struct plat_smp_ops *mp_ops;	/* private */

	return mp_ops->cpu_disable();
}

static inline void __cpu_die(unsigned int cpu)
{
	extern const struct plat_smp_ops *mp_ops;	/* private */

	mp_ops->cpu_die(cpu);
}

extern void play_dead(void);
#endif

/**
 * smp_get_online_sibling() - Get the CPU number of an online sibling CPU
 * @cpu: the CPU to which a sibling is sought
 *
 * Return: The CPU of an online sibling to @cpu, or -1 if there is none
 */
static inline int smp_get_online_sibling(int cpu)
{
	int sibling_cpu;

	/* Look for another online VP(E) within the core */
	for_each_online_cpu(sibling_cpu) {
		if (sibling_cpu == cpu)
			continue;
		if (cpus_are_siblings(cpu, sibling_cpu))
			return sibling_cpu;
	}

	/* No online sibling */
	return -1;
}

/*
 * This function will set up the necessary IPIs for Linux to communicate
 * with the CPUs in mask.
 * Return 0 on success.
 */
int mips_smp_ipi_allocate(const struct cpumask *mask);

/*
 * This function will free up IPIs allocated with mips_smp_ipi_allocate to the
 * CPUs in mask, which must be a subset of the IPIs that have been configured.
 * Return 0 on success.
 */
int mips_smp_ipi_free(const struct cpumask *mask);

static inline void arch_send_call_function_single_ipi(int cpu)
{
	extern const struct plat_smp_ops *mp_ops;	/* private */

	mp_ops->send_ipi_mask(cpumask_of(cpu), SMP_CALL_FUNCTION);
}

static inline void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	extern const struct plat_smp_ops *mp_ops;	/* private */

	mp_ops->send_ipi_mask(mask, SMP_CALL_FUNCTION);
}

#endif /* __ASM_SMP_H */
