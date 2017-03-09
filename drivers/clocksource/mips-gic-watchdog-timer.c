/*
 * Copyright (C) 2017 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/mfd/syscon.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <linux/regmap.h>
#include <linux/smp.h>
#include <linux/time.h>

struct mips_gic_watchdog_cevt_device {
	struct clock_event_device dev;
	struct regmap *rmap;
	u32 freq;
};

static DEFINE_PER_CPU(struct mips_gic_watchdog_cevt_device, cevt_device);

#define GEN_ACCESSORS(offset, name)					\
static inline u32							\
wdt_read_##name(struct mips_gic_watchdog_cevt_device *cd)		\
{									\
	u32 val;							\
	int err;							\
									\
	err = regmap_read(cd->rmap, offset, &val);			\
	return WARN_ON(err) ? 0 : val;					\
}									\
									\
static inline void							\
wdt_write_##name(struct mips_gic_watchdog_cevt_device *cd, u32 val)	\
{									\
	int err;							\
									\
	err = regmap_write(cd->rmap, offset, val);			\
	WARN_ON(err);							\
}

GEN_ACCESSORS(VPE_LOCAL_SECTION_OFS + GIC_VPE_WD_CONFIG0_OFS, config)
GEN_ACCESSORS(VPE_LOCAL_SECTION_OFS + GIC_VPE_WD_COUNT0_OFS, count)
GEN_ACCESSORS(VPE_LOCAL_SECTION_OFS + GIC_VPE_WD_INITIAL0_OFS, initial)

#define WDT_CONFIG_INTR			BIT(6)
#define WDT_CONFIG_WAIT			BIT(5)
#define WDT_CONFIG_DEBUG		BIT(4)
#define WDT_CONFIG_TYPE_ONESHOT		(0x0 << 1)
#define WDT_CONFIG_TYPE_SECOND_RESET	(0x1 << 1)
#define WDT_CONFIG_TYPE_PIT		(0x2 << 1)
#define WDT_CONFIG_START		BIT(0)

static inline struct mips_gic_watchdog_cevt_device *
dev_to_wdt(struct clock_event_device *dev)
{
	return container_of(dev, struct mips_gic_watchdog_cevt_device, dev);
}

static irqreturn_t gic_watchdog_interrupt(int irq, void *dev_id)
{
	struct mips_gic_watchdog_cevt_device *cd = dev_id;
	u32 cfg;

	cfg = wdt_read_config(cd);
	if (unlikely(WARN(!(cfg & WDT_CONFIG_INTR), "Spurious WDT interrupt")))
		return IRQ_NONE;

	cd->dev.event_handler(&cd->dev);

	/* Acknowledge the interrupt */
	wdt_write_config(cd, cfg);

	return IRQ_HANDLED;
}

struct irqaction mips_gic_watchdog_irqaction = {
	.handler = gic_watchdog_interrupt,
	.percpu_dev_id = &cevt_device,
	.flags = IRQF_PERCPU | IRQF_TIMER,
	.name = "watchdog-timer",
};

/**
 * set_next_event() - Setup the next oneshot event
 * @delta: the desired number of ticks until the next event
 * @dev: the clock event device
 *
 * Setup the GIC watchdog timer to provide a oneshot event @delta ticks into
 * the future.
 *
 * We rely here on the fact that this function is always called with interrupts
 * disabled, which means we don't race with the gic_watchdog_interrupt()
 * handler whilst manipulating GIC registers.
 *
 * Returns 0 indicating success, never fails.
 */
static int set_next_event(unsigned long delta, struct clock_event_device *dev)
{
	struct mips_gic_watchdog_cevt_device *cd = dev_to_wdt(dev);

	/* Clear any interrupt & stop the counter */
	wdt_write_config(cd, WDT_CONFIG_INTR);

	/* Set the initial count */
	wdt_write_initial(cd, delta);

	/* Start counting! */
	wdt_write_config(cd, WDT_CONFIG_WAIT | WDT_CONFIG_DEBUG |
			     WDT_CONFIG_TYPE_ONESHOT | WDT_CONFIG_START);
	return 0;
}

/**
 * set_state_periodic() - Setup periodic events
 * @dev: the clock event device
 *
 * Setup the GIC watchdog timer to provide events at a rate of HZ events per
 * second. In GIC terminology configure the watchdog in its Programmable
 * Interrupt Timer (PIT) mode.
 *
 * We rely here on the fact that this function is always called with interrupts
 * disabled, which means we don't race with the gic_watchdog_interrupt()
 * handler whilst manipulating GIC registers.
 *
 * Returns 0 indicating success, never fails.
 */
static int set_state_periodic(struct clock_event_device *dev)
{
	struct mips_gic_watchdog_cevt_device *cd = dev_to_wdt(dev);

	/* Clear any interrupt & stop the counter */
	wdt_write_config(cd, WDT_CONFIG_INTR);

	/* Set the initial count */
	wdt_write_initial(cd, DIV_ROUND_CLOSEST(cd->freq, HZ));

	/* Start counting! */
	wdt_write_config(cd, WDT_CONFIG_WAIT | WDT_CONFIG_DEBUG |
			     WDT_CONFIG_TYPE_PIT | WDT_CONFIG_START);
	return 0;
}

/**
 * set_state_shutdown() - Stop the clock
 * @dev: the clock event device
 *
 * Stop the GIC watchdog timer from counting, cancelling any pending events.
 *
 * We rely here on the fact that this function is always called with interrupts
 * disabled, which means we don't race with the gic_watchdog_interrupt()
 * handler whilst manipulating GIC registers.
 *
 * Returns 0 indicating success, never fails.
 */
static int set_state_shutdown(struct clock_event_device *dev)
{
	struct mips_gic_watchdog_cevt_device *cd = dev_to_wdt(dev);

	/* Clear any interrupt & stop the counter */
	wdt_write_config(cd, WDT_CONFIG_INTR);
	return 0;
}

static int cpu_starting(unsigned int cpu)
{
	struct mips_gic_watchdog_cevt_device *cd = this_cpu_ptr(&cevt_device);

	set_state_shutdown(&cd->dev);
	clockevents_config_and_register(&cd->dev, cd->freq,
					0x10000, 0xffffffff);
	enable_percpu_irq(cd->dev.irq, IRQ_TYPE_NONE);

	return 0;
}

static int cpu_dying(unsigned int cpu)
{
	struct mips_gic_watchdog_cevt_device *cd = this_cpu_ptr(&cevt_device);

	set_state_shutdown(&cd->dev);
	disable_percpu_irq(cd->dev.irq);
	return 0;
}

static int __init mips_gic_watchdog_timer_init(struct device_node *node)
{
	struct mips_gic_watchdog_cevt_device *cd;
	struct regmap *rmap;
	struct clk *clk;
	int ret, irq, cpu;
	u32 gic_frequency;

	if (!gic_present || !node->parent ||
	    !of_device_is_compatible(node->parent, "mti,gic")) {
		pr_warn("No DT definition for the mips gic driver");
		return -ENXIO;
	}

	rmap = syscon_node_to_regmap(node->parent);
	if (IS_ERR(rmap)) {
		pr_warn("GIC Watchdog unavailable because GIC is not a syscon");
		return PTR_ERR(rmap);
	}

	clk = of_clk_get(node, 0);
	if (!IS_ERR(clk)) {
		if (clk_prepare_enable(clk) < 0) {
			pr_err("GIC failed to enable clock\n");
			clk_put(clk);
			return PTR_ERR(clk);
		}

		gic_frequency = clk_get_rate(clk);
	} else if (of_property_read_u32(node, "clock-frequency",
					&gic_frequency)) {
		pr_err("GIC frequency not specified.\n");
		return -EINVAL;;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		pr_err("GIC watchdog IRQ not specified.\n");
		return -EINVAL;
	}

	ret = setup_percpu_irq(irq, &mips_gic_watchdog_irqaction);
	if (ret < 0) {
		pr_err("GIC watchdog IRQ %d setup failed: %d\n", irq, ret);
		return ret;
	}

	for_each_possible_cpu(cpu) {
		cd = per_cpu_ptr(&cevt_device, cpu);
		cd->freq = gic_frequency;
		cd->rmap = rmap;

		cd->dev.name = "MIPS GIC Watchdog";
		cd->dev.features = CLOCK_EVT_FEAT_ONESHOT |
				   CLOCK_EVT_FEAT_PERIODIC |
				   CLOCK_EVT_FEAT_C3STOP |
				   CLOCK_EVT_FEAT_PERCPU;

		cd->dev.set_next_event = set_next_event;
		cd->dev.set_state_oneshot = set_state_shutdown;
		cd->dev.set_state_oneshot_stopped = set_state_shutdown;
		cd->dev.set_state_periodic = set_state_periodic;
		cd->dev.set_state_shutdown = set_state_shutdown;

		cd->dev.cpumask = cpumask_of(cpu);
		cd->dev.irq = irq;

		cd->dev.rating = 400;
	}

	cpuhp_setup_state(CPUHP_AP_MIPS_GIC_WATCHDOG_TIMER_STARTING,
			  "clockevents/mips/gic/watchdog-timer:starting",
			  cpu_starting, cpu_dying);

	return 0;
}
TIMER_OF_DECLARE(mips_gic_watchdog_timer,
		 "mti,gic-watchdog-timer",
		 mips_gic_watchdog_timer_init);
