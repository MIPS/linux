/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/hotplug.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/bcache.h>
#include <asm/mips-cps.h>
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

static unsigned core_vpe_count(unsigned int cluster, unsigned core)
{
	if (threads_disabled)
		return 1;

	return mips_cps_numvps(cluster, core);
}

static void __init cps_smp_setup(void)
{
	unsigned int nclusters, ncores, nvpes, core_vpes;
	unsigned long core_entry;
	int cl, c, v;

	/* Detect & record VPE topology */
	nvpes = 0;
	nclusters = mips_cps_numclusters();
	pr_info("%s topology ", cpu_has_mips_r6 ? "VP" : "VPE");
	for (cl = 0; cl < nclusters; cl++) {
		if (cl > 0)
			pr_cont(",");
		pr_cont("{");

		ncores = mips_cps_numcores(cl);
		for (c = 0; c < ncores; c++) {
			core_vpes = core_vpe_count(cl, c);

			if (c > 0)
				pr_cont(",");
			pr_cont("%u", core_vpes);

			/* Use the number of VPEs in cluster 0 core 0 for smp_num_siblings */
			if (!cl && !c)
				smp_num_siblings = core_vpes;

			for (v = 0; v < min_t(int, core_vpes, NR_CPUS - nvpes); v++) {
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
	if (mips_cm_revision() >= CM_REV_CM3)
		write_gcr_bev_base(core_entry);

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpumask_set_cpu(0, &mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */
}

static void __init cps_prepare_cpus(unsigned int max_cpus)
{
	unsigned nclusters, ncores, core_vpes, c, cl, cca;
	bool cca_unsuitable, cores_limited;
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
	cores_limited = false;
	if (cca_unsuitable || cpu_has_dc_aliases) {
		for_each_present_cpu(c) {
			if (cpus_are_siblings(smp_processor_id(), c))
				continue;

			set_cpu_present(c, false);
			cores_limited = true;
		}
	}
	if (cores_limited)
		pr_warn("Using only one core due to %s%s%s\n",
			cca_unsuitable ? "unsuitable CCA" : "",
			(cca_unsuitable && cpu_has_dc_aliases) ? " & " : "",
			cpu_has_dc_aliases ? "dcache aliasing" : "");

	/*
	 * Patch the start of mips_cps_core_entry to provide:
	 *
	 * s0 = kseg0 CCA
	 */
	entry_code = (u32 *)&mips_cps_core_entry;
#ifdef __nanomips__
	*entry_code++ = 0x9008d000 | cca;
#else
	uasm_i_addiu(&entry_code, 16, 0, cca);
#endif
	blast_dcache_range((unsigned long)&mips_cps_core_entry,
			   (unsigned long)entry_code);
	bc_wback_inv((unsigned long)&mips_cps_core_entry,
		     (void *)entry_code - (void *)&mips_cps_core_entry);
	__sync();

	/* Allocate cluster boot configuration structs */
	nclusters = mips_cps_numclusters();
	mips_cps_cluster_bootcfg = kcalloc(nclusters,
					   sizeof(*mips_cps_cluster_bootcfg),
					   GFP_KERNEL);

	for (cl = 0; cl < nclusters; cl++) {
		/* Allocate core boot configuration structs */
		ncores = mips_cps_numcores(cl);
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
			ncores = mips_cps_numcores(cl);
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

static void init_cluster_l2(void)
{
	u32 l2_cfg, l2sm_cop, result;

	while (1) {
		l2_cfg = read_gcr_redir_l2_ram_config();

		/* If HCI is not supported, use the state machine below */
		if (!(l2_cfg & CM_GCR_L2_RAM_CONFIG_PRESENT))
			break;
		if (!(l2_cfg & CM_GCR_L2_RAM_CONFIG_HCI_SUPPORTED))
			break;

		/* If the HCI_DONE bit is set, we're finished */
		if (l2_cfg & CM_GCR_L2_RAM_CONFIG_HCI_DONE)
			return;
	}

	l2sm_cop = read_gcr_redir_l2sm_cop();
	if (WARN(!(l2sm_cop & CM_GCR_L2SM_COP_PRESENT),
		 "L2 init not supported on this system yet"))
		return;

	/* Clear L2 tag registers */
	write_gcr_redir_l2_tag_state(0);
	write_gcr_redir_l2_ecc(0);
	mb();

	/* Wait for the L2 state machine to be idle */
	do {
		l2sm_cop = read_gcr_redir_l2sm_cop();
	} while (l2sm_cop & CM_GCR_L2SM_COP_RUNNING);

	/* Start a store tag operation */
	l2sm_cop = CM_GCR_L2SM_COP_TYPE_IDX_STORETAG << __ffs(CM_GCR_L2SM_COP_TYPE);
	l2sm_cop |= CM_GCR_L2SM_COP_CMD_START;
	write_gcr_redir_l2sm_cop(l2sm_cop);
	mb();

	/* Wait for the operation to be complete */
	do {
		l2sm_cop = read_gcr_redir_l2sm_cop();
		result = l2sm_cop & CM_GCR_L2SM_COP_RESULT;
		result >>= __ffs(CM_GCR_L2SM_COP_RESULT);
	} while (!result);

	WARN(result != CM_GCR_L2SM_COP_RESULT_DONE_OK,
	     "L2 state machine failed cache init with error %u\n", result);
}

static void boot_core(unsigned int cluster, unsigned int core,
		      unsigned int vpe_id)
{
	struct cluster_boot_config *cluster_cfg;
	u32 access, stat, seq_state;
	unsigned int timeout, ncores;
	unsigned long core_entry;

	cluster_cfg = &mips_cps_cluster_bootcfg[cluster];
	ncores = mips_cps_numcores(cluster);
	core_entry = CKSEG1ADDR((unsigned long)mips_cps_core_entry);

	if ((cluster != cpu_cluster(&current_cpu_data)) &&
	    bitmap_empty(cluster_cfg->core_power, ncores)) {
		/* Set endianness & power up the CM */
		mips_cm_lock_other(cluster, 0, 0, CM_GCR_Cx_OTHER_BLOCK_GLOBAL);
		write_cpc_redir_sys_config(IS_ENABLED(CONFIG_CPU_BIG_ENDIAN));
		write_cpc_redir_pwrup_ctl(1);
		mips_cm_unlock_other();

		/* Wait for the CM to start up */
		timeout = 1000;
		mips_cm_lock_other(cluster, CM_GCR_Cx_OTHER_CORE_CM, 0,
				   CM_GCR_Cx_OTHER_BLOCK_LOCAL);
		while (1) {
			stat = read_cpc_co_stat_conf();
			seq_state = stat & CPC_Cx_STAT_CONF_SEQSTATE;
			seq_state >>= __ffs(CPC_Cx_STAT_CONF_SEQSTATE);
			if (seq_state == CPC_Cx_STAT_CONF_SEQSTATE_U5)
				break;

			if (timeout) {
				mdelay(1);
				timeout--;
			} else {
				pr_warn("Waiting for cluster %u CM to power up... STAT_CONF=0x%x\n",
					cluster, stat);
				mdelay(1000);
			}
		}
		mips_cm_unlock_other();

		mips_cm_lock_other(cluster, core, 0, CM_GCR_Cx_OTHER_BLOCK_GLOBAL);

		/* Ensure cluster GCRs are where we expect */
		write_gcr_redir_base(read_gcr_base());
		write_gcr_redir_cpc_base(read_gcr_cpc_base());
		write_gcr_redir_gic_base(read_gcr_gic_base());

		init_cluster_l2();

		/* Mirror L2 configuration */
		write_gcr_redir_l2_only_sync_base(read_gcr_l2_only_sync_base());
		write_gcr_redir_l2_pft_control(read_gcr_l2_pft_control());
		write_gcr_redir_l2_pft_control_b(read_gcr_l2_pft_control_b());

		/* Mirror ECC/parity setup */
		write_gcr_redir_err_control(read_gcr_err_control());

		/* Set BEV base */
		write_gcr_redir_bev_base(core_entry);

		mips_cm_unlock_other();
	}

	if (cluster != cpu_cluster(&current_cpu_data)) {
		mips_cm_lock_other(cluster, core, 0, CM_GCR_Cx_OTHER_BLOCK_GLOBAL);

		/* Ensure the core can access the GCRs */
		access = read_gcr_redir_access();
		access |= BIT(core);
		write_gcr_redir_access(access);

		mips_cm_unlock_other();
	} else {
		/* Ensure the core can access the GCRs */
		access = read_gcr_access();
		access |= BIT(core);
		write_gcr_access(access);
	}

	/* Select the appropriate core */
	mips_cm_lock_other(cluster, core, 0, CM_GCR_Cx_OTHER_BLOCK_LOCAL);

	/* Set its reset vector */
	write_gcr_co_reset_base(core_entry);

	/* Ensure its coherency is disabled */
	write_gcr_co_coherence(0);

	/* Start it with the legacy memory map and exception base */
	write_gcr_co_reset_ext_base(CM_GCR_Cx_RESET_EXT_BASE_UEB);

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
			seq_state = stat & CPC_Cx_STAT_CONF_SEQSTATE;
			seq_state >>= __ffs(CPC_Cx_STAT_CONF_SEQSTATE);

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

	/*
	 * Restore CM_PWRUP=0 so that the CM can power down if all the cores in
	 * the cluster do (eg. if they're all removed via hotplug.
	 */
	if (mips_cm_revision() >= CM_REV_CM3_5) {
		mips_cm_lock_other(cluster, 0, 0, CM_GCR_Cx_OTHER_BLOCK_GLOBAL);
		write_cpc_redir_pwrup_ctl(0);
		mips_cm_unlock_other();
	}
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

static int cps_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned cluster = cpu_cluster(&cpu_data[cpu]);
	unsigned core = cpu_core(&cpu_data[cpu]);
	unsigned vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	struct cluster_boot_config *cluster_cfg =
		&mips_cps_cluster_bootcfg[cluster];
	struct core_boot_config *core_cfg = &cluster_cfg->core_config[core];
	struct vpe_boot_config *vpe_cfg = &core_cfg->vpe_config[vpe_id];
	unsigned long core_entry;
	int remote;
	int err;

	vpe_cfg->pc = (unsigned long)&smp_bootstrap;
	vpe_cfg->sp = __KSTK_TOS(idle);
	vpe_cfg->gp = (unsigned long)task_thread_info(idle);

	atomic_or(1 << cpu_vpe_id(&cpu_data[cpu]), &core_cfg->vpe_mask);

	preempt_disable();

	if (!test_bit(core, cluster_cfg->core_power)) {
		/* Boot a VPE on a powered down core */
		boot_core(cluster, core, vpe_id);
		goto out;
	}

	if (cpu_has_vp) {
		mips_cm_lock_other(cluster, core, vpe_id, CM_GCR_Cx_OTHER_BLOCK_LOCAL);
		core_entry = CKSEG1ADDR((unsigned long)mips_cps_core_entry);
		write_gcr_co_reset_base(core_entry);
		mips_cm_unlock_other();
	}

	if (!cpus_are_siblings(cpu, smp_processor_id())) {
		remote = smp_get_online_sibling(cpu);
		if (remote < 0) {
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
	return 0;
}

static void cps_init_secondary(void)
{
	/* Disable MT - we only want to run 1 TC per VPE */
	if (cpu_has_mipsmt)
		dmt();

	if ((mips_cm_revision() >= CM_REV_CM3) ||
	    mips_cm_is_2_6()) {
		unsigned int ident = read_gic_vl_ident();

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

	/*
	 * Check that the PM code will be able to perform the power down if this
	 * is the last CPU to be offlined in a core. If not, block the attempt
	 */
	if ((smp_get_online_sibling(cpu) < 0) &&
	    (!cps_pm_support_state(CPS_PM_POWER_GATED)))
		return -EINVAL;

	cluster_cfg = &mips_cps_cluster_bootcfg[cpu_cluster(&current_cpu_data)];
	core_cfg = &cluster_cfg->core_config[cpu_core(&current_cpu_data)];
	atomic_sub(1 << cpu_vpe_id(&current_cpu_data), &core_cfg->vpe_mask);
	smp_mb__after_atomic();
	set_cpu_online(cpu, false);
	calculate_cpu_foreign_map();

	return 0;
}

static int cpu_death_sibling;
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
		cpu_death_sibling = smp_get_online_sibling(cpu);
		if (cpu_death_sibling >= 0) {
			/*
			 * There is an online VPE within the core. Just halt
			 * this TC and leave the core alone.
			 */
			cpu_death = CPU_DEATH_HALT;
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
	ktime_t fail_time;
	unsigned stat;
	int err;
	struct cluster_boot_config *cluster_cfg;

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
			mips_cm_lock_other(0, core, 0, CM_GCR_Cx_OTHER_BLOCK_LOCAL);
			mips_cpc_lock_other(core);
			stat = read_cpc_co_stat_conf();
			stat &= CPC_Cx_STAT_CONF_SEQSTATE;
			stat >>= __ffs(CPC_Cx_STAT_CONF_SEQSTATE);
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
				 cpu, stat))
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
			mips_cm_lock_other(0, core, vpe_id, CM_GCR_Cx_OTHER_BLOCK_LOCAL);
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
	if (!(read_gcr_gic_status() & CM_GCR_GIC_STATUS_EX)) {
		pr_warn("MIPS CPS SMP unable to proceed without a GIC\n");
		return -ENODEV;
	}

	register_smp_ops(&cps_smp_ops);
	return 0;
}
