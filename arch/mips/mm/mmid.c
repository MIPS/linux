/*
 * Based on arch/arm64/mm/context.c
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 * Copyright (C) 2012 ARM Ltd.
 * Copyrignt (C) 2017 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/smp.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>

unsigned long mmid_mask;

static u32 mmid_bits;
static DEFINE_RAW_SPINLOCK(cpu_mmid_lock);

static atomic64_t mmid_generation;
static unsigned long *mmid_map;

static DEFINE_PER_CPU(atomic64_t, active_mmids);
static DEFINE_PER_CPU(u64, reserved_mmids);
static cpumask_t tlb_flush_pending;

#define MMID_MASK		(GENMASK(mmid_bits - 1, 0))
#define MMID_FIRST_VERSION	(1UL << mmid_bits)
#define NUM_USER_MMIDS		MMID_FIRST_VERSION

#define MAX_MMID_BITS		16

static void flush_context(unsigned int cpu)
{
	int i;
	u64 mmid;

	/* Update the list of reserved MMIDs and the MMID bitmap. */
	bitmap_clear(mmid_map, 0, NUM_USER_MMIDS);

	/*
	 * Ensure the generation bump is observed before we xchg the
	 * active_mmids.
	 */
	smp_wmb();

	for_each_possible_cpu(i) {
		mmid = atomic64_xchg_relaxed(&per_cpu(active_mmids, i), 0);
		/*
		 * If this CPU has already been through a
		 * rollover, but hasn't run another task in
		 * the meantime, we must preserve its reserved
		 * MMID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (mmid == 0)
			mmid = per_cpu(reserved_mmids, i);
		__set_bit(mmid & MMID_MASK, mmid_map);
		per_cpu(reserved_mmids, i) = mmid;
	}

	/* Queue a TLB invalidate */
	cpumask_setall(&tlb_flush_pending);
}

static bool check_update_reserved_mmid(u64 mmid, u64 newmmid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved MMIDs looking for a match.
	 * If we find one, then we can update our mm to use newmmid
	 * (i.e. the same MMID in the current generation) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old MMID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved MMID in a future
	 * generation.
	 */
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_mmids, cpu) == mmid) {
			hit = true;
			per_cpu(reserved_mmids, cpu) = newmmid;
		}
	}

	return hit;
}

static u64 refresh_context(struct mm_struct *mm, unsigned int cpu)
{
	static u32 cur_idx = 1;
	u64 mmid = atomic64_read(&mm->context.mmid);
	u64 generation = atomic64_read(&mmid_generation);

	if (mmid != 0) {
		u64 newmmid = generation | (mmid & MMID_MASK);

		/*
		 * If our current MMID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
		if (check_update_reserved_mmid(mmid, newmmid))
			return newmmid;

		/*
		 * We had a valid MMID in a previous life, so try to re-use
		 * it if possible.
		 */
		mmid &= MMID_MASK;
		if (!__test_and_set_bit(mmid, mmid_map))
			return newmmid;
	}

	/*
	 * Allocate a free MMID. If we can't find one, take a note of the
	 * currently active MMIDs and mark the TLBs as requiring flushes.
	 *
	 * We don't allocate MMID #0 in the first generation such that we
	 * can use cpu_context()==0 to indicate that a struct mm has never
	 * been used.
	 */
	mmid = find_next_zero_bit(mmid_map, NUM_USER_MMIDS, cur_idx);
	if (mmid != NUM_USER_MMIDS)
		goto set_mmid;

	/* We're out of MMIDs, so increment the global generation count */
	generation = atomic64_add_return_relaxed(MMID_FIRST_VERSION,
						 &mmid_generation);
	flush_context(cpu);

	/* We have more MMIDs than CPUs, so this will always succeed */
	mmid = find_next_zero_bit(mmid_map, NUM_USER_MMIDS, 1);

set_mmid:
	__set_bit(mmid, mmid_map);
	cur_idx = mmid;
	return mmid | generation;
}

void switch_mmid(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long flags;
	u64 mmid;

	mmid = atomic64_read(&mm->context.mmid);

	/*
	 * The memory ordering here is subtle. We rely on the control
	 * dependency between the generation read and the update of
	 * active_mmids to ensure that we are synchronised with a
	 * parallel rollover (i.e. this pairs with the smp_wmb() in
	 * flush_context).
	 */
	if (!((mmid ^ atomic64_read(&mmid_generation)) >> mmid_bits)
	    && atomic64_xchg_relaxed(&per_cpu(active_mmids, cpu), mmid)) {
		write_c0_memorymapid(mmid & mmid_mask);
		goto out;
	}

	raw_spin_lock_irqsave(&cpu_mmid_lock, flags);
	/* Check that our MMID belongs to the current generation. */
	mmid = atomic64_read(&mm->context.mmid);
	if ((mmid ^ atomic64_read(&mmid_generation)) >> mmid_bits) {
		mmid = refresh_context(mm, cpu);
		atomic64_set(&mm->context.mmid, mmid);
	}

	if (cpumask_test_cpu(cpu, &tlb_flush_pending)) {
		local_flush_tlb_all();
		cpumask_clear_cpu(cpu, &tlb_flush_pending);
	}

	atomic64_set(&per_cpu(active_mmids, cpu), mmid);
	raw_spin_unlock_irqrestore(&cpu_mmid_lock, flags);

	/* Set the MemoryMapID */
	write_c0_memorymapid(mmid & mmid_mask);

#ifdef CONFIG_SMP
	/*
	 * If this CPU shares FTLB entries with its siblings and one or more
	 * of those siblings hasn't yet invalidated/flushed its TLB following
	 * the start of a new generation then we need to invalidate any TLB
	 * entries for our new MMID that we might otherwise pick up from a
	 * sibling.
	 */
	if (cpu_has_shared_ftlb_entries &&
	    cpumask_intersects(&tlb_flush_pending, &cpu_sibling_map[cpu])) {
		/* Ensure the new MMID takes effect */
		mtc0_tlbw_hazard();

		/* Invalidate TLB entries for our new MMID */
		global_tlb_invalidate(0, invalidate_by_mmid);
	}
#endif

out:
	if (cpu_has_vtag_icache)
		flush_icache_all();
}

static int mips_mmid_disabled;

static int __init mmid_disable(char *s)
{
	mips_mmid_disabled = 1;
	return 1;
}

__setup("nommid", mmid_disable);

static unsigned int mmid_max_bits;

static int __init setup_mmid_max_bits(char *s)
{
	int err = kstrtouint(s, 0, &mmid_max_bits);

	return err ?: 1;
}
__setup("mmid_max_bits=", setup_mmid_max_bits);

void setup_mmid(void)
{
	unsigned int orig, config5;

	orig = config5 = read_c0_config5();

	if (IS_ENABLED(CONFIG_MIPS_MMID_SUPPORT) && !mips_mmid_disabled)
		config5 |= MIPS_CONF5_MI;
	else
		config5 &= ~MIPS_CONF5_MI;

	write_c0_config5(config5);
	back_to_back_c0_hazard();
	config5 = read_c0_config5();

	if (config5 & MIPS_CONF5_MI) {
		current_cpu_data.options |= MIPS_CPU_MMID;

		/* We need support for MMID if we couldn't disable it */
		WARN(!IS_ENABLED(CONFIG_MIPS_MMID_SUPPORT),
		     "Unable to disable MMID support, but kernel support is disabled");

		/* Ensure we match the boot CPU */
		WARN(!cpu_has_mmid, "CPUs have differing MMID support");
	} else {
		/* Ensure we match the boot CPU */
		WARN(cpu_has_mmid, "CPUs have differing MMID support");
	}

	/* TLB state is unpredictable after changing Config5.MI */
	if ((orig ^ config5) & MIPS_CONF5_MI)
		local_flush_tlb_all();
}

int __init mmid_init(void)
{
	setup_mmid();

	if (!cpu_has_mmid)
		return 0;

	write_c0_memorymapid(~0);
	back_to_back_c0_hazard();
	mmid_mask = read_c0_memorymapid();

	if (mmid_max_bits && (mmid_mask >= BIT(mmid_max_bits)))
		mmid_mask = GENMASK(mmid_max_bits - 1, 0);

	mmid_bits = min(get_bitmask_order(mmid_mask), MAX_MMID_BITS);

	/*
	 * Expect allocation after rollover to fail if we don't have at least
	 * one more MMID than CPUs.
	 */
	WARN_ON(NUM_USER_MMIDS - 1 <= num_possible_cpus());
	atomic64_set(&mmid_generation, MMID_FIRST_VERSION);
	mmid_map = kzalloc(BITS_TO_LONGS(NUM_USER_MMIDS) * sizeof(*mmid_map),
			   GFP_KERNEL);
	if (!mmid_map)
		panic("Failed to allocate bitmap for %lu MMIDs (%ld)\n",
		      NUM_USER_MMIDS,
		      BITS_TO_LONGS(NUM_USER_MMIDS)*sizeof(*mmid_map));

	pr_info("MMID allocator initialised with %lu entries\n",
		NUM_USER_MMIDS);
	return 0;
}
early_initcall(mmid_init);
