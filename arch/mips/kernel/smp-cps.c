/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/hotplug.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/bcache.h>
#include <asm/mips-cm.h>
#include <asm/mips-cpc.h>
#include <asm/mips_mt.h>
#include <asm/mipsregs.h>
#include <asm/pm-cps.h>
#include <asm/r4kcache.h>
#include <asm/smp-cps.h>
#include <asm/time.h>
#include <asm/uasm.h>

static bool threads_disabled;

struct cluster_boot_config *mips_cps_cluster_bootcfg;

static int __init setup_nothreads(char *s)
{
	threads_disabled = true;
	return 0;
}
early_param("nothreads", setup_nothreads);

#ifdef CONFIG_MIPS_CPU_STEAL
struct cpumask cpu_stolen_mask;

static inline bool cpu_stolen(int cpu)
{
	return cpumask_test_cpu(cpu, &cpu_stolen_mask);
}

static inline void set_cpu_stolen(int cpu, bool state)
{
	if (state)
		cpumask_set_cpu(cpu, &cpu_stolen_mask);
	else
		cpumask_clear_cpu(cpu, &cpu_stolen_mask);
}
#else
static inline bool cpu_stolen(int cpu)
{
	return false;
}

static inline void set_cpu_stolen(int cpu, bool state) { }

#endif /* CONFIG_MIPS_CPU_STEAL */

static void power_up_other_cluster(unsigned int cluster)
{
	unsigned long timeout, stat_conf, seq_state;

	mips_cm_lock_other(cluster, 0x20, 0, BLOCK_CPC_CORE_LOCAL);
	stat_conf = read_cpc_co_stat_conf();
	mips_cm_unlock_other();

	/* If the CM is powered & coherent, we're done */
	seq_state = stat_conf & CPC_Cx_STAT_CONF_SEQSTATE_MSK;
	if (seq_state == CPC_Cx_STAT_CONF_SEQSTATE_U5)
		return;

	/* Power up the CM */
	mips_cm_lock_other(cluster, 0, 0, BLOCK_CPC_GLOBAL);
	write_redir_cpc_pwrup_ctl(1);
	mips_cm_unlock_other();

	mips_cm_lock_other(cluster, 0x20, 0, BLOCK_CPC_CORE_LOCAL);

	/* Wait for the CM to start up */
	timeout = 1000;
	while (1) {
		stat_conf = read_cpc_co_stat_conf();
		seq_state = stat_conf & CPC_Cx_STAT_CONF_SEQSTATE_MSK;
		if (seq_state == CPC_Cx_STAT_CONF_SEQSTATE_U5)
			break;

		if (timeout) {
			mdelay(1);
			timeout--;
		} else {
			pr_warn("Waiting for cluster %u CM to power up... STAT_CONF=0x%lx\n",
				cluster, stat_conf);
			mdelay(1000);
		}
	}

	mips_cm_unlock_other();
}

static unsigned int
core_vpe_count(unsigned int cluster, unsigned int core)
{
	unsigned cfg;

	if (threads_disabled)
		return 1;

	if ((!IS_ENABLED(CONFIG_MIPS_MT_SMP) || !cpu_has_mipsmt)
		&& (!IS_ENABLED(CONFIG_CPU_MIPSR6) || !cpu_has_vp))
		return 1;

	mips_cm_lock_other(cluster, core, 0, BLOCK_GCR_CORE_LOCAL);
	cfg = read_gcr_co_config() & CM_GCR_Cx_CONFIG_PVPE_MSK;
	mips_cm_unlock_other();
	return (cfg >> CM_GCR_Cx_CONFIG_PVPE_SHF) + 1;
}

static void __init cps_smp_setup(void)
{
	unsigned int nclusters, ncores, nvpes, core_vpes;
	unsigned long core_entry;
	int cl, c, v;

	/* Detect & record VPE topology */
	pr_info("%s topology ", cpu_has_mips_r6 ? "VP" : "VPE");
	nclusters = mips_cm_numclusters();
	nvpes = 0;
	for (cl = 0; cl < nclusters; cl++) {
		pr_cont("%s", cl ? ",{" : "{");

		if (mips_cm_revision() >= CM_REV_CM3_5)
			power_up_other_cluster(cl);

		ncores = mips_cm_numcores(cl);

		for (c = 0; c < ncores; c++) {
			core_vpes = core_vpe_count(0, c);
			pr_cont("%s%u", c ? "," : "", core_vpes);

			/*
			 * Use the number of VPEs in core 0 for
			 * smp_num_siblings
			 */
			if (!c)
				smp_num_siblings = core_vpes;

			for (v = 0;
			     v < min_t(int, core_vpes, NR_CPUS - nvpes);
			     v++) {
				cpu_set_cluster(&cpu_data[nvpes + v], cl);
				cpu_set_core(&cpu_data[nvpes + v], c);
				cpu_set_vpe_id(&cpu_data[nvpes + v], v);
			}

			nvpes += core_vpes;
		}

		pr_cont("}");
	}
	pr_cont(" total %u\n", nvpes);

	/* Indicate present CPUs (CPU being synonymous with VPE) */
	for (v = 0; v < min_t(unsigned, nvpes, NR_CPUS); v++) {
		set_cpu_possible(v, true);
		set_cpu_present(v, true);
		__cpu_number_map[v] = v;
		__cpu_logical_map[v] = v;
	}

	/* Set a coherent default CCA (CWB) */
	change_c0_config(CONF_CM_CMASK, 0x5);

	/* Initialise core 0 */
	mips_cps_core_init();

	/* Make core 0 coherent with everything */
	write_gcr_cl_coherence(0xff);

	core_entry = CKSEG1ADDR((unsigned long)mips_cps_core_entry);
	if (mips_cm_revision() >= CM_REV_CM3_5) {
		for (cl = 0; cl < nclusters; cl++) {
			mips_cm_lock_other(cl, 0, 0, BLOCK_GCR_GLOBAL);
			write_redir_gcr_bev_base(core_entry);
			mips_cm_unlock_other();
		}
	} else if (mips_cm_revision() >= CM_REV_CM3) {
		write_gcr_bev_base(core_entry);
	}

#ifdef CONFIG_MIPS_CPU_STEAL
	cpumask_clear(&cpu_stolen_mask);
#endif /* CONFIG_MIPS_CPU_STEAL */

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpumask_set_cpu(0, &mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */
}

static void __init cps_prepare_cpus(unsigned int max_cpus)
{
	unsigned nclusters, ncores, core_vpes, c, cl, cca;
	bool cca_unsuitable, any_disabled;
	u32 *entry_code;
	struct cluster_boot_config *cluster_bootcfg;
	struct core_boot_config *core_bootcfg;

	mips_mt_set_cpuoptions();

	/* Detect whether the CCA is unsuited to multi-core SMP */
	cca = read_c0_config() & CONF_CM_CMASK;
	switch (cca) {
	case 0x4: /* CWBE */
	case 0x5: /* CWB */
		/* The CCA is coherent, multi-core is fine */
		cca_unsuitable = false;
		break;

	default:
		/* CCA is not coherent, multi-core is not usable */
		cca_unsuitable = true;
	}

	/* Warn the user if the CCA prevents multi-core */
	ncores = mips_cm_numcores(0);
	any_disabled = false;
	if (cca_unsuitable || cpu_has_dc_aliases) {
		for_each_present_cpu(c) {
			if (!cpu_cluster(&cpu_data[c]) && !cpu_core(&cpu_data[c]))
				continue;

			set_cpu_present(c, false);
			any_disabled = true;
		}
	}

	if (any_disabled) {
		pr_warn("Using only one core due to ");
		if (cca_unsuitable)
			pr_cont("unsuitable CCA 0x%x ", cca);
		if (cca_unsuitable && cpu_has_dc_aliases)
			pr_cont("& ");
		if (cpu_has_dc_aliases)
			pr_cont("dcache aliasing");
		pr_cont("\n");
	}

	/*
	 * Patch the start of mips_cps_core_entry to provide:
	 *
	 * s0 = kseg0 CCA
	 */
	entry_code = (u32 *)&mips_cps_core_entry;
	uasm_i_addiu(&entry_code, 16, 0, cca);
	blast_dcache_range((unsigned long)&mips_cps_core_entry,
			   (unsigned long)entry_code);
	bc_wback_inv((unsigned long)&mips_cps_core_entry,
		     (void *)entry_code - (void *)&mips_cps_core_entry);
	__sync();

	/* Allocate cluster boot configuration structs */
	nclusters = mips_cm_numclusters();
	mips_cps_cluster_bootcfg = kcalloc(nclusters,
					   sizeof(*mips_cps_cluster_bootcfg),
					   GFP_KERNEL);

	for (cl = 0; cl < nclusters; cl++) {
		/* Allocate core boot configuration structs */
		ncores = mips_cm_numcores(cl);
		core_bootcfg = kcalloc(ncores, sizeof(*core_bootcfg),
					GFP_KERNEL);
		if (!core_bootcfg) {
			pr_err("Failed to allocate boot config for %u cores\n", ncores);
			goto err_out;
		}
		mips_cps_cluster_bootcfg[cl].core_config = core_bootcfg;

		mips_cps_cluster_bootcfg[cl].core_power =
			kcalloc(BITS_TO_LONGS(ncores), sizeof(unsigned long),
				GFP_KERNEL);

		/* Allocate VPE boot configuration structs */
		for (c = 0; c < ncores; c++) {
			core_vpes = core_vpe_count(cl, c);
			core_bootcfg[c].vpe_config = kcalloc(core_vpes,
					sizeof(*core_bootcfg[c].vpe_config),
					GFP_KERNEL);
			if (!core_bootcfg[c].vpe_config) {
				pr_err("Failed to allocate %u VPE boot configs\n",
				       core_vpes);
				goto err_out;
			}
		}
	}

	/* Mark this CPU as powered up & booted */
	cluster_bootcfg =
		&mips_cps_cluster_bootcfg[cpu_cluster(&current_cpu_data)];
	bitmap_set(cluster_bootcfg->core_power, cpu_core(&current_cpu_data), 1);
	core_bootcfg = &cluster_bootcfg->core_config[cpu_core(&current_cpu_data)];
	atomic_set(&core_bootcfg->vpe_mask, 1 << cpu_vpe_id(&current_cpu_data));

	return;
err_out:
	/* Clean up allocations */
	if (mips_cps_cluster_bootcfg) {
		for (cl = 0; cl < nclusters; cl++) {
			cluster_bootcfg = &mips_cps_cluster_bootcfg[cl];
			ncores = mips_cm_numcores(cl);
			for (c = 0; c < ncores; c++) {
				core_bootcfg = &cluster_bootcfg->core_config[c];
				kfree(core_bootcfg->vpe_config);
			}
			kfree(mips_cps_cluster_bootcfg[c].core_config);
		}
		kfree(mips_cps_cluster_bootcfg);
		mips_cps_cluster_bootcfg = NULL;
	}

	/* Effectively disable SMP by declaring CPUs not present */
	for_each_possible_cpu(c) {
		if (c == 0)
			continue;
		set_cpu_present(c, false);
	}
}

static void boot_core(unsigned int cluster, unsigned int core,
		      unsigned int vpe_id)
{
	struct cluster_boot_config *cluster_cfg;
	u32 access, stat, seq_state, l2_cfg, l2sm_cop;
	unsigned int timeout, ncores;

	cluster_cfg = &mips_cps_cluster_bootcfg[cluster];
	ncores = mips_cm_numcores(cluster);

	if ((cluster != cpu_cluster(&current_cpu_data)) &&
	    bitmap_empty(cluster_cfg->core_power, ncores)) {
		/* Power up cluster */
		power_up_other_cluster(cluster);

		mips_cm_lock_other(cluster, core, 0, BLOCK_GCR_GLOBAL);

		/* Ensure cluster GCRs are where we expect */
		write_redir_gcr_base(read_gcr_base());
		write_redir_gcr_cpc_base(read_gcr_cpc_base());
		write_redir_gcr_gic_base(read_gcr_gic_base());

		l2_cfg = read_redir_gcr_l2_ram_config();
		l2sm_cop = read_redir_gcr_l2sm_cop();

		if ((l2_cfg & CM_HCR_L2_RAM_CONFIG_PRESENT) &&
		    (l2_cfg & CM_HCR_L2_RAM_CONFIG_HCI_SUPPORTED)) {
			while (!(l2_cfg & CM_HCR_L2_RAM_CONFIG_HCI_DONE)) {
				/* Wait for L2 config to be complete */
				l2_cfg = read_redir_gcr_l2_ram_config();
			}
		} else if (l2sm_cop & CM_GCR_L2SM_COP_PRESENT) {
			/* Initialise L2 cache */
			mips_cm_l2sm_cacheop(L2SM_COP_INDEX_STORE_TAG, 0, 0);
		} else {
			WARN(1, "L2 init not supported on this system yet\n");
		}

		/* Mirror L2 configuration */
		write_redir_gcr_l2_only_sync_base(read_gcr_l2_only_sync_base());
		write_redir_gcr_l2_pft_control(read_gcr_l2_pft_control());
		write_redir_gcr_l2_pft_control_b(read_gcr_l2_pft_control_b());

		/* Mirror ECC/parity setup */
		write_redir_gcr_err_control(read_gcr_err_control());

		mips_cm_unlock_other();
	}

	if (cluster != cpu_cluster(&current_cpu_data)) {
		mips_cm_lock_other(cluster, core, 0, BLOCK_GCR_GLOBAL);

		/* Ensure the core can access the GCRs */
		access = read_redir_gcr_access();
		access |= 1 << (CM_GCR_ACCESS_ACCESSEN_SHF + core);
		write_redir_gcr_access(access);

		mips_cm_unlock_other();
	} else {
		/* Ensure the core can access the GCRs */
		access = read_gcr_access();
		access |= 1 << (CM_GCR_ACCESS_ACCESSEN_SHF + core);
		write_gcr_access(access);
	}

	/* Select the appropriate core */
	mips_cm_lock_other(cluster, core, 0, BLOCK_GCR_CORE_LOCAL);

	/* Set its reset vector */
	write_gcr_co_reset_base(CKSEG1ADDR((unsigned long)mips_cps_core_entry));

	/* Ensure its coherency is disabled */
	write_gcr_co_coherence(0);

	/* Start it with the legacy memory map and exception base */
	write_gcr_co_reset_ext_base(CM_GCR_RESET_EXT_BASE_UEB);

	if (mips_cpc_present()) {
		/* Reset the core */
		mips_cpc_lock_other(core);

		if (mips_cm_revision() >= CM_REV_CM3) {
			/* Run only the requested VP following the reset */
			write_cpc_co_vp_stop(0xf);
			write_cpc_co_vp_run(1 << vpe_id);

			/*
			 * Ensure that the VP_RUN register is written before the
			 * core leaves reset.
			 */
			wmb();
		}

		write_cpc_co_cmd(CPC_Cx_CMD_RESET);

		timeout = 100;
		while (true) {
			stat = read_cpc_co_stat_conf();
			seq_state = stat & CPC_Cx_STAT_CONF_SEQSTATE_MSK;

			/* U6 == coherent execution, ie. the core is up */
			if (seq_state == CPC_Cx_STAT_CONF_SEQSTATE_U6)
				break;

			/* Delay a little while before we start warning */
			if (timeout) {
				timeout--;
				mdelay(10);
				continue;
			}

			pr_warn("Waiting for core %u to start... STAT_CONF=0x%x\n",
				core, stat);
			mdelay(1000);
		}

		mips_cpc_unlock_other();
	} else {
		/* Take the core out of reset */
		write_gcr_co_reset_release(0);
	}

	mips_cm_unlock_other();

	/* The core is now powered up */
	bitmap_set(cluster_cfg->core_power, core, 1);
}

static void remote_vpe_boot(void *dummy)
{
	unsigned cluster = cpu_cluster(&current_cpu_data);
	unsigned core = cpu_core(&current_cpu_data);
	struct cluster_boot_config *cluster_cfg =
		&mips_cps_cluster_bootcfg[cluster];
	struct core_boot_config *core_cfg = &cluster_cfg->core_config[core];

	mips_cps_boot_vpes(core_cfg, cpu_vpe_id(&current_cpu_data));
}

static void cps_start_secondary(int cpu, void *entry_fn, struct task_struct *tsk)
{
	unsigned int cluster = cpu_cluster(&cpu_data[cpu]);
	unsigned core = cpu_core(&cpu_data[cpu]);
	unsigned vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	struct cluster_boot_config *cluster_cfg =
		&mips_cps_cluster_bootcfg[cluster];
	struct core_boot_config *core_cfg = &cluster_cfg->core_config[core];
	struct vpe_boot_config *vpe_cfg = &core_cfg->vpe_config[vpe_id];
	unsigned long core_entry;
	unsigned int remote;
	int err;

	vpe_cfg->pc = (unsigned long)entry_fn;
	vpe_cfg->sp = __KSTK_TOS(tsk);
	vpe_cfg->gp = (unsigned long)task_thread_info(tsk);

	atomic_or(1 << cpu_vpe_id(&cpu_data[cpu]), &core_cfg->vpe_mask);

	preempt_disable();

	if (!test_bit(core, cluster_cfg->core_power)) {
		/* Boot a VPE on a powered down core */
		boot_core(cluster, core, vpe_id);
		goto out;
	}

	if (cpu_has_vp) {
		mips_cm_lock_other(cluster, core, vpe_id, BLOCK_GCR_CORE_LOCAL);
		core_entry = CKSEG1ADDR((unsigned long)mips_cps_core_entry);
		write_gcr_co_reset_base(core_entry);
		mips_cm_unlock_other();
	}

	if (!cpus_are_siblings(cpu, smp_processor_id())) {
		/* Boot a VPE on another powered up core */
		for (remote = 0; remote < NR_CPUS; remote++) {
			if (!cpus_are_siblings(cpu, remote))
				continue;
			if (cpu_online(remote))
				break;
		}
		if (remote >= NR_CPUS) {
			pr_crit("No online CPU in core %u to start CPU%d\n",
				core, cpu);
			goto out;
		}

		err = smp_call_function_single(remote, remote_vpe_boot,
					       NULL, 1);
		if (err)
			panic("Failed to call remote CPU\n");
		goto out;
	}

	BUG_ON(!cpu_has_mipsmt && !cpu_has_vp);

	/* Boot a VPE on this core */
	mips_cps_boot_vpes(core_cfg, vpe_id);
out:
	preempt_enable();
}

static void cps_boot_secondary(int cpu, struct task_struct *idle)
{
	cps_start_secondary(cpu, &smp_bootstrap, idle);
}

static void cps_init_secondary(void)
{
	/* Disable MT - we only want to run 1 TC per VPE */
	if (cpu_has_mipsmt)
		dmt();

	if (mips_cm_revision() >= CM_REV_CM3) {
		unsigned ident = gic_read_local_vp_id();

		/*
		 * Ensure that our calculation of the VP ID matches up with
		 * what the GIC reports, otherwise we'll have configured
		 * interrupts incorrectly.
		 */
		BUG_ON(ident != mips_cm_vp_id(smp_processor_id()));
	}

	if (cpu_has_veic)
		clear_c0_status(ST0_IM);
	else
		change_c0_status(ST0_IM, STATUSF_IP2 | STATUSF_IP3 |
					 STATUSF_IP4 | STATUSF_IP5 |
					 STATUSF_IP6 | STATUSF_IP7);
}

static void cps_smp_finish(void)
{
	write_c0_compare(read_c0_count() + (8 * mips_hpt_frequency / HZ));

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpumask_set_cpu(smp_processor_id(), &mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */

	local_irq_enable();
}

#ifdef CONFIG_HOTPLUG_CPU

static int cps_cpu_disable(void)
{
	unsigned cpu = smp_processor_id();
	struct cluster_boot_config *cluster_cfg;
	struct core_boot_config *core_cfg;

	if (!cpu)
		return -EBUSY;

	if (!cps_pm_support_state(CPS_PM_POWER_GATED))
		return -EINVAL;

#ifdef CONFIG_MIPS_CPU_STEAL
	/*
	 * With the MT ASE only VPEs in the same core may read / write the
	 * control registers of other VPEs. Therefore to maintain control of
	 * any stolen VPEs at least one sibling VPE must be kept online.
	 */
	if (cpu_has_mipsmt) {
		int stolen, siblings = 0;

		for_each_cpu((stolen), &cpu_stolen_mask)
			if (cpus_are_siblings(stolen, cpu))
				siblings++;

		if (siblings == 1)
			/*
			 * When a VPE has been stolen, keep at least one of it's
			 * siblings around in order to control it.
			 */
			return -EBUSY;
	}
#endif /* CONFIG_MIPS_CPU_STEAL */

	cluster_cfg = &mips_cps_cluster_bootcfg[cpu_cluster(&current_cpu_data)];
	core_cfg = &cluster_cfg->core_config[cpu_core(&current_cpu_data)];
	atomic_sub(1 << cpu_vpe_id(&current_cpu_data), &core_cfg->vpe_mask);
	smp_mb__after_atomic();
	set_cpu_online(cpu, false);
	calculate_cpu_foreign_map();

	return 0;
}

static unsigned cpu_death_sibling;
static enum {
	CPU_DEATH_HALT,
	CPU_DEATH_POWER,
} cpu_death;

void play_dead(void)
{
	unsigned int cpu, core, vpe_id;

	local_irq_disable();
	idle_task_exit();
	cpu = smp_processor_id();
	core = cpu_core(&cpu_data[cpu]);
	cpu_death = CPU_DEATH_POWER;

	pr_debug("CPU%d going offline\n", cpu);

	if (cpu_has_mipsmt || cpu_has_vp) {
		/* Look for another online VPE within the core */
		for_each_possible_cpu(cpu_death_sibling) {
			if (!cpus_are_siblings(cpu, cpu_death_sibling))
				continue;

			/*
			 * There is an online VPE within the core. Just halt
			 * this TC and leave the core alone.
			 */
			if (cpu_online(cpu_death_sibling) ||
			    cpu_stolen(cpu_death_sibling))
				cpu_death = CPU_DEATH_HALT;
			if (cpu_online(cpu_death_sibling))
				break;
		}
	}

	/* This CPU has chosen its way out */
	(void)cpu_report_death();

	if (cpu_death == CPU_DEATH_HALT) {
		vpe_id = cpu_vpe_id(&cpu_data[cpu]);

		pr_debug("Halting core %d VP%d\n", core, vpe_id);
		if (cpu_has_mipsmt) {
			/* Halt this TC */
			write_c0_tchalt(TCHALT_H);
			instruction_hazard();
		} else if (cpu_has_vp) {
			write_cpc_cl_vp_stop(1 << vpe_id);

			/* Ensure that the VP_STOP register is written */
			wmb();
		}
	} else {
		pr_debug("Gating power to core %d\n", core);
		/* Power down the core */
		cps_pm_enter_state(CPS_PM_POWER_GATED);
	}

	/* This should never be reached */
	panic("Failed to offline CPU %u", cpu);
}

#ifdef CONFIG_MIPS_CPU_STEAL

/* Find an online sibling CPU (another VPE in the same core) */
static inline int mips_cps_get_online_sibling(unsigned int cpu)
{
	int sibling;

	for_each_online_cpu(sibling)
		if (cpus_are_siblings(sibling, cpu))
			return sibling;

	return -1;
}

int mips_cps_steal_cpu_and_execute(unsigned int cpu, void *entry_fn,
				   struct task_struct *tsk)
{
	int err = -EINVAL;

	preempt_disable();

	if (!cpu_present(cpu) || cpu_online(cpu) || cpu_stolen(cpu))
		goto out;

	if (cpu_has_mipsmt && (mips_cps_get_online_sibling(cpu) < 0))
		pr_warn("CPU%d has no online siblings to control it\n", cpu);
	else {
		set_cpu_present(cpu, false);
		set_cpu_stolen(cpu, true);

		cps_start_secondary(cpu, entry_fn, tsk);
		err = 0;
	}
out:
	preempt_enable();
	return err;
}

static void mips_cps_halt_sibling(void *ptr_cpu)
{
	unsigned int cpu = (unsigned long)ptr_cpu;
	unsigned int vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	unsigned long flags;
	int vpflags;

	local_irq_save(flags);
	vpflags = dvpe();
	settc(vpe_id);
	write_tc_c0_tchalt(TCHALT_H);
	evpe(vpflags);
	local_irq_restore(flags);
}

int mips_cps_halt_and_return_cpu(unsigned int cpu)
{
	unsigned int vpe_id = cpu_vpe_id(&cpu_data[cpu]);

	if (!cpu_stolen(cpu))
		return -EINVAL;

	if (cpu_has_mipsmt && cpus_are_siblings(cpu, smp_processor_id()))
		mips_cps_halt_sibling((void *)(unsigned long)cpu);
	else if (cpu_has_mipsmt) {
		int sibling = mips_cps_get_online_sibling(cpu);

		if (sibling < 0) {
			pr_warn("CPU%d has no online siblings\n", cpu);
			return -EINVAL;
		}

		if (smp_call_function_single(sibling, mips_cps_halt_sibling,
						(void *)(unsigned long)cpu, 1))
			panic("Failed to call sibling CPU\n");

	} else if (cpu_has_vp) {
		mips_cm_lock_other_cpu(cpu, BLOCK_CPC_CORE_LOCAL);
		write_cpc_co_vp_stop(1 << vpe_id);
		mips_cm_unlock_other();
	}

	set_cpu_stolen(cpu, false);
	set_cpu_present(cpu, true);
	return 0;
}

#endif /* CONFIG_MIPS_CPU_STEAL */

static void wait_for_sibling_halt(void *ptr_cpu)
{
	unsigned cpu = (unsigned long)ptr_cpu;
	unsigned vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	unsigned halted;
	unsigned long flags;

	do {
		local_irq_save(flags);
		settc(vpe_id);
		halted = read_tc_c0_tchalt();
		local_irq_restore(flags);
	} while (!(halted & TCHALT_H));
}

static void cps_cpu_die(unsigned int cpu)
{
	unsigned cluster = cpu_cluster(&cpu_data[cpu]);
	unsigned core = cpu_core(&cpu_data[cpu]);
	unsigned int vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	unsigned stat;
	int err;
	struct cluster_boot_config *cluster_cfg;
	ktime_t fail_time;

	cluster_cfg = &mips_cps_cluster_bootcfg[cluster];

	/* Wait for the cpu to choose its way out */
	if (!cpu_wait_death(cpu, 5)) {
		pr_err("CPU%u: didn't offline\n", cpu);
		return;
	}

	/*
	 * Now wait for the CPU to actually offline. Without doing this that
	 * offlining may race with one or more of:
	 *
	 *   - Onlining the CPU again.
	 *   - Powering down the core if another VPE within it is offlined.
	 *   - A sibling VPE entering a non-coherent state.
	 *
	 * In the non-MT halt case (ie. infinite loop) the CPU is doing nothing
	 * with which we could race, so do nothing.
	 */
	if (cpu_death == CPU_DEATH_POWER) {
		/*
		 * Wait for the core to enter a powered down or clock gated
		 * state, the latter happening when a JTAG probe is connected
		 * in which case the CPC will refuse to power down the core.
		 */
		fail_time = ktime_add_ms(ktime_get(), 2000);
		do {
			mips_cm_lock_other(cluster, core, 0,
					   BLOCK_GCR_CORE_LOCAL);
			mips_cpc_lock_other(core);
			stat = read_cpc_co_stat_conf();
			stat &= CPC_Cx_STAT_CONF_SEQSTATE_MSK;
			mips_cpc_unlock_other();
			mips_cm_unlock_other();

			if (stat == CPC_Cx_STAT_CONF_SEQSTATE_D0 ||
			    stat == CPC_Cx_STAT_CONF_SEQSTATE_D2 ||
			    stat == CPC_Cx_STAT_CONF_SEQSTATE_U2)
				break;

			/*
			 * The core ought to have powered down, but didn't &
			 * now we don't really know what state it's in. It's
			 * likely that its _pwr_up pin has been wired to logic
			 * 1 & it powered back up as soon as we powered it
			 * down...
			 *
			 * The best we can do is warn the user & continue in
			 * the hope that the core is doing nothing harmful &
			 * might behave properly if we online it later.
			 */
			if (WARN(ktime_after(ktime_get(), fail_time),
				 "CPU%u hasn't powered down, seq. state %u\n",
				 cpu, stat >> CPC_Cx_STAT_CONF_SEQSTATE_SHF))
				break;
		} while (1);

		/* Indicate the core is powered off */
		bitmap_clear(cluster_cfg->core_power, core, 1);
	} else if (cpu_has_mipsmt) {
		/*
		 * Have a CPU with access to the offlined CPUs registers wait
		 * for its TC to halt.
		 */
		err = smp_call_function_single(cpu_death_sibling,
					       wait_for_sibling_halt,
					       (void *)(unsigned long)cpu, 1);
		if (err)
			panic("Failed to call remote sibling CPU\n");
	} else if (cpu_has_vp) {
		do {
			mips_cm_lock_other(cluster, core, vpe_id,
					   BLOCK_GCR_CORE_LOCAL);
			stat = read_cpc_co_vp_running();
			mips_cm_unlock_other();
		} while (stat & (1 << vpe_id));
	}
}

#endif /* CONFIG_HOTPLUG_CPU */

static const struct plat_smp_ops cps_smp_ops = {
	.smp_setup		= cps_smp_setup,
	.prepare_cpus		= cps_prepare_cpus,
	.boot_secondary		= cps_boot_secondary,
	.init_secondary		= cps_init_secondary,
	.smp_finish		= cps_smp_finish,
	.send_ipi_single	= mips_smp_send_ipi_single,
	.send_ipi_mask		= mips_smp_send_ipi_mask,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= cps_cpu_disable,
	.cpu_die		= cps_cpu_die,
#endif
};

bool mips_cps_smp_in_use(void)
{
	extern const struct plat_smp_ops *mp_ops;
	return mp_ops == &cps_smp_ops;
}

int register_cps_smp_ops(void)
{
	if (!mips_cm_present()) {
		pr_warn("MIPS CPS SMP unable to proceed without a CM\n");
		return -ENODEV;
	}

	/* check we have a GIC - we need one for IPIs */
	if (!(read_gcr_gic_status() & CM_GCR_GIC_STATUS_EX_MSK)) {
		pr_warn("MIPS CPS SMP unable to proceed without a GIC\n");
		return -ENODEV;
	}

	register_smp_ops(&cps_smp_ops);
	return 0;
}
