// SPDX-License-Identifier: GPL-2.0
/*
 * JZ47xx SoCs TCU IRQ driver
 * Copyright (C) 2018 Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched_clock.h>

#include <dt-bindings/clock/ingenic,tcu.h>

#include "ingenic-timer.h"

/* 8 channels max + watchdog + OST */
#define TCU_CLK_COUNT	10

enum tcu_clk_parent {
	TCU_PARENT_PCLK,
	TCU_PARENT_RTC,
	TCU_PARENT_EXT,
};

struct ingenic_soc_info {
	unsigned char num_channels;
	bool has_ost;
};

struct ingenic_tcu_clk_info {
	struct clk_init_data init_data;
	u8 gate_bit;
	u8 tcsr_reg;
};

struct ingenic_tcu_clk {
	struct clk_hw hw;

	struct regmap *map;
	const struct ingenic_tcu_clk_info *info;

	unsigned int idx;
};

#define to_tcu_clk(_hw) container_of(_hw, struct ingenic_tcu_clk, hw)

struct ingenic_tcu {
	const struct ingenic_soc_info *soc_info;
	struct regmap *map;
	struct clk *clk, *timer_clk, *cs_clk;

	struct irq_domain *domain;
	unsigned int nb_parent_irqs;
	u32 parent_irqs[3];

	struct clk_hw_onecell_data *clocks;

	unsigned int timer_channel, cs_channel;
	struct clock_event_device cevt;
	struct clocksource cs;
	char name[4];
};

static struct ingenic_tcu *ingenic_tcu;

void __iomem *ingenic_tcu_base;
EXPORT_SYMBOL_GPL(ingenic_tcu_base);

static int ingenic_tcu_enable(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;

	regmap_write(tcu_clk->map, TCU_REG_TSCR, BIT(info->gate_bit));
	return 0;
}

static void ingenic_tcu_disable(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;

	regmap_write(tcu_clk->map, TCU_REG_TSSR, BIT(info->gate_bit));
}

static int ingenic_tcu_is_enabled(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	unsigned int value;

	regmap_read(tcu_clk->map, TCU_REG_TSR, &value);

	return !(value & BIT(info->gate_bit));
}

static u8 ingenic_tcu_get_parent(struct clk_hw *hw)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	unsigned int val = 0;
	int ret;

	ret = regmap_read(tcu_clk->map, info->tcsr_reg, &val);
	WARN_ONCE(ret < 0, "Unable to read TCSR %i", tcu_clk->idx);

	return ffs(val & TCU_TCSR_PARENT_CLOCK_MASK) - 1;
}

static int ingenic_tcu_set_parent(struct clk_hw *hw, u8 idx)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	struct regmap *map = tcu_clk->map;
	int ret;

	/*
	 * Our clock provider has the CLK_SET_PARENT_GATE flag set, so we know
	 * that the clk is in unprepared state. To be able to access TCSR
	 * we must ungate the clock supply and we gate it again when done.
	 */

	regmap_write(map, TCU_REG_TSCR, BIT(info->gate_bit));

	ret = regmap_update_bits(map, info->tcsr_reg,
				TCU_TCSR_PARENT_CLOCK_MASK, BIT(idx));
	WARN_ONCE(ret < 0, "Unable to update TCSR %i", tcu_clk->idx);

	regmap_write(map, TCU_REG_TSSR, BIT(info->gate_bit));

	return 0;
}

static unsigned long ingenic_tcu_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	unsigned int prescale;
	int ret;

	ret = regmap_read(tcu_clk->map, info->tcsr_reg, &prescale);
	WARN_ONCE(ret < 0, "Unable to read TCSR %i", tcu_clk->idx);

	prescale = (prescale & TCU_TCSR_PRESCALE_MASK) >> TCU_TCSR_PRESCALE_LSB;

	return parent_rate >> (prescale * 2);
}

static u8 ingenic_tcu_get_prescale(unsigned long rate, unsigned long req_rate)
{
	u8 prescale;

	for (prescale = 0; prescale < 5; prescale++)
		if ((rate >> (prescale * 2)) <= req_rate)
			return prescale;

	return 5; /* /1024 divider */
}

static long ingenic_tcu_round_rate(struct clk_hw *hw, unsigned long req_rate,
		unsigned long *parent_rate)
{
	unsigned long rate = *parent_rate;
	u8 prescale;

	if (req_rate > rate)
		return -EINVAL;

	prescale = ingenic_tcu_get_prescale(rate, req_rate);
	return rate >> (prescale * 2);
}

static int ingenic_tcu_set_rate(struct clk_hw *hw, unsigned long req_rate,
		unsigned long parent_rate)
{
	struct ingenic_tcu_clk *tcu_clk = to_tcu_clk(hw);
	const struct ingenic_tcu_clk_info *info = tcu_clk->info;
	struct regmap *map = tcu_clk->map;
	u8 prescale = ingenic_tcu_get_prescale(parent_rate, req_rate);
	int ret;

	/*
	 * Our clock provider has the CLK_SET_RATE_GATE flag set, so we know
	 * that the clk is in unprepared state. To be able to access TCSR
	 * we must ungate the clock supply and we gate it again when done.
	 */

	regmap_write(map, TCU_REG_TSCR, BIT(info->gate_bit));

	ret = regmap_update_bits(map, info->tcsr_reg,
				TCU_TCSR_PRESCALE_MASK,
				prescale << TCU_TCSR_PRESCALE_LSB);
	WARN_ONCE(ret < 0, "Unable to update TCSR %i", tcu_clk->idx);

	regmap_write(map, TCU_REG_TSSR, BIT(info->gate_bit));

	return 0;
}

static const struct clk_ops ingenic_tcu_clk_ops = {
	.get_parent	= ingenic_tcu_get_parent,
	.set_parent	= ingenic_tcu_set_parent,

	.recalc_rate	= ingenic_tcu_recalc_rate,
	.round_rate	= ingenic_tcu_round_rate,
	.set_rate	= ingenic_tcu_set_rate,

	.enable		= ingenic_tcu_enable,
	.disable	= ingenic_tcu_disable,
	.is_enabled	= ingenic_tcu_is_enabled,
};

static const char * const ingenic_tcu_timer_parents[] = {
	[TCU_PARENT_PCLK] = "pclk",
	[TCU_PARENT_RTC]  = "rtc",
	[TCU_PARENT_EXT]  = "ext",
};

#define DEF_TIMER(_name, _gate_bit, _tcsr)				\
	{								\
		.init_data = {						\
			.name = _name,					\
			.parent_names = ingenic_tcu_timer_parents,	\
			.num_parents = ARRAY_SIZE(ingenic_tcu_timer_parents),\
			.ops = &ingenic_tcu_clk_ops,			\
			.flags = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE,\
		},							\
		.gate_bit = _gate_bit,					\
		.tcsr_reg = _tcsr,					\
	}
static const struct ingenic_tcu_clk_info ingenic_tcu_clk_info[] = {
	[TCU_CLK_TIMER0] = DEF_TIMER("timer0", 0, TCU_REG_TCSRc(0)),
	[TCU_CLK_TIMER1] = DEF_TIMER("timer1", 1, TCU_REG_TCSRc(1)),
	[TCU_CLK_TIMER2] = DEF_TIMER("timer2", 2, TCU_REG_TCSRc(2)),
	[TCU_CLK_TIMER3] = DEF_TIMER("timer3", 3, TCU_REG_TCSRc(3)),
	[TCU_CLK_TIMER4] = DEF_TIMER("timer4", 4, TCU_REG_TCSRc(4)),
	[TCU_CLK_TIMER5] = DEF_TIMER("timer5", 5, TCU_REG_TCSRc(5)),
	[TCU_CLK_TIMER6] = DEF_TIMER("timer6", 6, TCU_REG_TCSRc(6)),
	[TCU_CLK_TIMER7] = DEF_TIMER("timer7", 7, TCU_REG_TCSRc(7)),
};

static const struct ingenic_tcu_clk_info ingenic_tcu_watchdog_clk_info =
				DEF_TIMER("wdt", 16, TCU_REG_WDT_TCSR);
static const struct ingenic_tcu_clk_info ingenic_tcu_ost_clk_info =
				DEF_TIMER("ost", 15, TCU_REG_OST_TCSR);
#undef DEF_TIMER

static void ingenic_tcu_intc_cascade(struct irq_desc *desc)
{
	struct irq_chip *irq_chip = irq_data_get_irq_chip(&desc->irq_data);
	struct irq_domain *domain = irq_desc_get_handler_data(desc);
	struct irq_chip_generic *gc = irq_get_domain_generic_chip(domain, 0);
	struct regmap *map = gc->private;
	uint32_t irq_reg, irq_mask;
	unsigned int i;

	regmap_read(map, TCU_REG_TFR, &irq_reg);
	regmap_read(map, TCU_REG_TMR, &irq_mask);

	chained_irq_enter(irq_chip, desc);

	irq_reg &= ~irq_mask;

	for_each_set_bit(i, (unsigned long *)&irq_reg, 32)
		generic_handle_irq(irq_linear_revmap(domain, i));

	chained_irq_exit(irq_chip, desc);
}

static void ingenic_tcu_gc_unmask_enable_reg(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct regmap *map = gc->private;
	u32 mask = d->mask;

	irq_gc_lock(gc);
	regmap_write(map, ct->regs.ack, mask);
	regmap_write(map, ct->regs.enable, mask);
	*ct->mask_cache |= mask;
	irq_gc_unlock(gc);
}

static void ingenic_tcu_gc_mask_disable_reg(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct regmap *map = gc->private;
	u32 mask = d->mask;

	irq_gc_lock(gc);
	regmap_write(map, ct->regs.disable, mask);
	*ct->mask_cache &= ~mask;
	irq_gc_unlock(gc);
}

static void ingenic_tcu_gc_mask_disable_reg_and_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = irq_data_get_chip_type(d);
	struct regmap *map = gc->private;
	u32 mask = d->mask;

	irq_gc_lock(gc);
	regmap_write(map, ct->regs.ack, mask);
	regmap_write(map, ct->regs.disable, mask);
	irq_gc_unlock(gc);
}

static u64 notrace ingenic_tcu_timer_read(void)
{
	unsigned int channel = ingenic_tcu->cs_channel;
	u16 count;

	count = readw(ingenic_tcu_base + TCU_REG_TCNTc(channel));

	return count;
}

static inline struct ingenic_tcu *to_ingenic_tcu(struct clock_event_device *evt)
{
	return container_of(evt, struct ingenic_tcu, cevt);
}

static int ingenic_tcu_cevt_set_state_shutdown(struct clock_event_device *evt)
{
	struct ingenic_tcu *tcu = to_ingenic_tcu(evt);

	regmap_write(tcu->map, TCU_REG_TECR, BIT(tcu->timer_channel));
	return 0;
}

static int ingenic_tcu_cevt_set_next(unsigned long next,
				     struct clock_event_device *evt)
{
	struct ingenic_tcu *tcu = to_ingenic_tcu(evt);

	if (next > 0xffff)
		return -EINVAL;

	regmap_write(tcu->map, TCU_REG_TDFRc(tcu->timer_channel), next);
	regmap_write(tcu->map, TCU_REG_TCNTc(tcu->timer_channel), 0);
	regmap_write(tcu->map, TCU_REG_TESR, BIT(tcu->timer_channel));

	return 0;
}

static irqreturn_t ingenic_tcu_cevt_cb(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	struct ingenic_tcu *tcu = to_ingenic_tcu(evt);

	regmap_write(tcu->map, TCU_REG_TECR, BIT(tcu->timer_channel));

	if (evt->event_handler)
		evt->event_handler(evt);

	return IRQ_HANDLED;
}

static int __init ingenic_tcu_register_clock(struct ingenic_tcu *tcu,
			unsigned int idx, enum tcu_clk_parent parent,
			const struct ingenic_tcu_clk_info *info,
			struct clk_hw_onecell_data *clocks)
{
	struct ingenic_tcu_clk *tcu_clk;
	int err;

	tcu_clk = kzalloc(sizeof(*tcu_clk), GFP_KERNEL);
	if (!tcu_clk)
		return -ENOMEM;

	tcu_clk->hw.init = &info->init_data;
	tcu_clk->idx = idx;
	tcu_clk->info = info;
	tcu_clk->map = tcu->map;

	/* Reset channel and clock divider, set default parent */
	ingenic_tcu_enable(&tcu_clk->hw);
	regmap_update_bits(tcu->map, info->tcsr_reg, 0xffff, BIT(parent));
	ingenic_tcu_disable(&tcu_clk->hw);

	err = clk_hw_register(NULL, &tcu_clk->hw);
	if (err)
		goto err_free_tcu_clk;

	err = clk_hw_register_clkdev(&tcu_clk->hw, info->init_data.name, NULL);
	if (err)
		goto err_clk_unregister;

	clocks->hws[idx] = &tcu_clk->hw;
	return 0;

err_clk_unregister:
	clk_hw_unregister(&tcu_clk->hw);
err_free_tcu_clk:
	kfree(tcu_clk);
	return err;
}

static int __init ingenic_tcu_clk_init(struct ingenic_tcu *tcu,
				       struct device_node *np)
{
	size_t i;
	int ret;

	tcu->clocks = kzalloc(sizeof(*tcu->clocks) +
			 sizeof(*tcu->clocks->hws) * TCU_CLK_COUNT,
			 GFP_KERNEL);
	if (!tcu->clocks)
		return -ENOMEM;

	tcu->clocks->num = TCU_CLK_COUNT;

	for (i = 0; i < tcu->soc_info->num_channels; i++) {
		ret = ingenic_tcu_register_clock(tcu, i, TCU_PARENT_EXT,
				&ingenic_tcu_clk_info[i], tcu->clocks);
		if (ret) {
			pr_err("ingenic-timer: cannot register clock %i\n", i);
			goto err_unregister_timer_clocks;
		}
	}

	/*
	 * We set EXT as the default parent clock for all the TCU clocks
	 * except for the watchdog one, where we set the RTC clock as the
	 * parent. Since the EXT and PCLK are much faster than the RTC clock,
	 * the watchdog would kick after a maximum time of 5s, and we might
	 * want a slower kicking time.
	 */
	ret = ingenic_tcu_register_clock(tcu, TCU_CLK_WDT, TCU_PARENT_RTC,
				&ingenic_tcu_watchdog_clk_info, tcu->clocks);
	if (ret) {
		pr_err("ingenic-timer: cannot register watchdog clock\n");
		goto err_unregister_timer_clocks;
	}

	if (tcu->soc_info->has_ost) {
		ret = ingenic_tcu_register_clock(tcu, TCU_CLK_OST,
					TCU_PARENT_EXT,
					&ingenic_tcu_ost_clk_info,
					tcu->clocks);
		if (ret) {
			pr_err("ingenic-timer: cannot register ost clock\n");
			goto err_unregister_watchdog_clock;
		}
	}

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, tcu->clocks);
	if (ret) {
		pr_err("ingenic-timer: cannot add OF clock provider\n");
		goto err_unregister_ost_clock;
	}

	return 0;

err_unregister_ost_clock:
	if (tcu->soc_info->has_ost)
		clk_hw_unregister(tcu->clocks->hws[i + 1]);
err_unregister_watchdog_clock:
	clk_hw_unregister(tcu->clocks->hws[i]);
err_unregister_timer_clocks:
	for (i = 0; i < tcu->clocks->num; i++)
		if (tcu->clocks->hws[i])
			clk_hw_unregister(tcu->clocks->hws[i]);
	kfree(tcu->clocks);
	return ret;
}

static void __init ingenic_tcu_clk_cleanup(struct ingenic_tcu *tcu,
					   struct device_node *np)
{
	unsigned int i;

	of_clk_del_provider(np);

	for (i = 0; i < tcu->clocks->num; i++)
		clk_hw_unregister(tcu->clocks->hws[i]);
	kfree(tcu->clocks);
}

static int __init ingenic_tcu_intc_init(struct ingenic_tcu *tcu,
					struct device_node *np)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int err, i, irqs;

	irqs = of_property_count_elems_of_size(np, "interrupts", sizeof(u32));
	if (irqs < 0 || irqs > ARRAY_SIZE(tcu->parent_irqs))
		return -EINVAL;

	tcu->nb_parent_irqs = irqs;

	tcu->domain = irq_domain_add_linear(np, 32,
			&irq_generic_chip_ops, NULL);
	if (!tcu->domain)
		return -ENOMEM;

	err = irq_alloc_domain_generic_chips(tcu->domain, 32, 1, "TCU",
			handle_level_irq, 0, IRQ_NOPROBE | IRQ_LEVEL, 0);
	if (err)
		goto out_domain_remove;

	gc = irq_get_domain_generic_chip(tcu->domain, 0);
	ct = gc->chip_types;

	gc->wake_enabled = IRQ_MSK(32);
	gc->private = tcu->map;

	ct->regs.disable = TCU_REG_TMSR;
	ct->regs.enable = TCU_REG_TMCR;
	ct->regs.ack = TCU_REG_TFCR;
	ct->chip.irq_unmask = ingenic_tcu_gc_unmask_enable_reg;
	ct->chip.irq_mask = ingenic_tcu_gc_mask_disable_reg;
	ct->chip.irq_mask_ack = ingenic_tcu_gc_mask_disable_reg_and_ack;
	ct->chip.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE;

	/* Mask all IRQs by default */
	regmap_write(tcu->map, TCU_REG_TMSR, IRQ_MSK(32));

	/* On JZ4740, timer 0 and timer 1 have their own interrupt line;
	 * timers 2-7 share one interrupt.
	 * On SoCs >= JZ4770, timer 5 has its own interrupt line;
	 * timers 0-4 and 6-7 share one single interrupt.
	 *
	 * To keep things simple, we just register the same handler to
	 * all parent interrupts. The handler will properly detect which
	 * channel fired the interrupt.
	 */
	for (i = 0; i < irqs; i++) {
		tcu->parent_irqs[i] = irq_of_parse_and_map(np, i);
		if (!tcu->parent_irqs[i]) {
			err = -EINVAL;
			goto out_unmap_irqs;
		}

		irq_set_chained_handler_and_data(tcu->parent_irqs[i],
				ingenic_tcu_intc_cascade, tcu->domain);
	}

	return 0;

out_unmap_irqs:
	for (; i > 0; i--)
		irq_dispose_mapping(tcu->parent_irqs[i - 1]);
out_domain_remove:
	irq_domain_remove(tcu->domain);
	return err;
}

static void __init ingenic_tcu_intc_cleanup(struct ingenic_tcu *tcu)
{
	unsigned int i;

	for (i = 0; i < tcu->nb_parent_irqs; i++)
		irq_dispose_mapping(tcu->parent_irqs[i]);

	irq_domain_remove(tcu->domain);
}

static int __init ingenic_tcu_timer_init(struct ingenic_tcu *tcu,
					 struct device_node *np)
{
	unsigned int timer_virq;
	unsigned long rate;
	int err;

	tcu->timer_clk = of_clk_get_by_name(np, "timer");

	err = clk_prepare_enable(tcu->timer_clk);
	if (err)
		return err;

	rate = clk_get_rate(tcu->timer_clk);
	if (!rate) {
		err = -EINVAL;
		goto err_clk_disable;
	}

	timer_virq = irq_of_parse_and_map(np, 0);
	if (!timer_virq) {
		err = -EINVAL;
		goto err_clk_disable;
	}

	snprintf(tcu->name, sizeof(tcu->name), "TCU");

	err = request_irq(timer_virq, ingenic_tcu_cevt_cb, IRQF_TIMER,
			  tcu->name, &tcu->cevt);
	if (err)
		goto err_irq_dispose_mapping;

	tcu->cevt.cpumask = cpumask_of(smp_processor_id());
	tcu->cevt.features = CLOCK_EVT_FEAT_ONESHOT;
	tcu->cevt.name = tcu->name;
	tcu->cevt.rating = 200;
	tcu->cevt.set_state_shutdown = ingenic_tcu_cevt_set_state_shutdown;
	tcu->cevt.set_next_event = ingenic_tcu_cevt_set_next;

	clockevents_config_and_register(&tcu->cevt, rate, 10, 0xffff);

	return 0;

err_irq_dispose_mapping:
	irq_dispose_mapping(timer_virq);
err_clk_disable:
	clk_disable_unprepare(tcu->timer_clk);
	return err;
}

static int __init ingenic_tcu_clocksource_init(struct ingenic_tcu *tcu,
					       struct device_node *np)
{
	unsigned int channel = tcu->cs_channel;
	struct clocksource *cs = &tcu->cs;
	unsigned long rate;
	int err;

	tcu->cs_clk = of_clk_get_by_name(np, "timer");

	err = clk_prepare_enable(tcu->cs_clk);
	if (err)
		return err;

	rate = clk_get_rate(tcu->cs_clk);
	if (!rate) {
		err = -EINVAL;
		goto err_clk_disable;
	}

	/* Reset channel */
	regmap_update_bits(tcu->map, TCU_REG_TCSRc(channel),
			   0xffff & ~TCU_TCSR_RESERVED_BITS, 0);

	/* Reset counter */
	regmap_write(tcu->map, TCU_REG_TDFRc(channel), 0xffff);
	regmap_write(tcu->map, TCU_REG_TCNTc(channel), 0);

	/* Enable channel */
	regmap_write(tcu->map, TCU_REG_TESR, BIT(channel));

	cs->name = "ingenic-timer";
	cs->rating = 200;
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS;
	cs->mask = CLOCKSOURCE_MASK(16);
	cs->read = (u64 (*)(struct clocksource *))ingenic_tcu_timer_read;

	err = clocksource_register_hz(cs, rate);
	if (err)
		goto err_clk_disable;

	sched_clock_register(ingenic_tcu_timer_read, 16, rate);

	return 0;

err_clk_disable:
	clk_disable_unprepare(tcu->cs_clk);
	return err;
}

static void __init ingenic_tcu_clocksource_cleanup(struct ingenic_tcu *tcu)
{
	if (tcu->cs_clk) {
		clocksource_unregister(&tcu->cs);
		clk_disable_unprepare(tcu->cs_clk);
	}
}

static int __init ingenic_tcu_get_tcu_channel(struct ingenic_tcu *tcu,
					      struct device_node *np,
					      struct resource *parent_res)
{
	int ret, channel;
	struct resource res;
	unsigned long offset;

	ret = of_address_to_resource(np, 0, &res);
	if (ret < 0)
		return ret;

	if ((res.start % TCU_CHANNEL_STRIDE) ||
				(resource_size(&res) != TCU_CHANNEL_STRIDE))
		return -EINVAL;

	offset = res.start - TCU_REG_TDFR0 - parent_res->start;
	channel = offset / TCU_CHANNEL_STRIDE;

	if (channel >= tcu->soc_info->num_channels)
		return -EINVAL;

	return channel;
}

static const struct regmap_config ingenic_tcu_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static const struct ingenic_soc_info jz4740_soc_info = {
	.num_channels = 8,
	.has_ost = false,
};

static const struct ingenic_soc_info jz4725b_soc_info = {
	.num_channels = 6,
	.has_ost = true,
};

static const struct ingenic_soc_info jz4770_soc_info = {
	.num_channels = 8,
	.has_ost = true,
};

static const struct of_device_id ingenic_tcu_of_match[] = {
	{ .compatible = "ingenic,jz4740-tcu",  .data = &jz4740_soc_info, },
	{ .compatible = "ingenic,jz4725b-tcu", .data = &jz4725b_soc_info, },
	{ .compatible = "ingenic,jz4770-tcu",  .data = &jz4770_soc_info, },
	{ }
};

static int __init ingenic_tcu_init(struct device_node *np)
{
	const struct of_device_id *id = of_match_node(ingenic_tcu_of_match, np);
	struct device_node *timer_node, *cs_node;
	struct ingenic_tcu *tcu;
	struct resource res;
	void __iomem *base;
	int ret;

	of_node_clear_flag(np, OF_POPULATED);

	tcu = kzalloc(sizeof(*tcu), GFP_KERNEL);
	if (!tcu)
		return -ENOMEM;

	tcu->soc_info = (const struct ingenic_soc_info *)id->data;
	ingenic_tcu = tcu;

	base = of_io_request_and_map(np, 0, NULL);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto err_free_ingenic_tcu;
	}

	of_address_to_resource(np, 0, &res);

	ingenic_tcu_base = base;

	tcu->map = regmap_init_mmio(NULL, base, &ingenic_tcu_regmap_config);
	if (IS_ERR(tcu->map)) {
		ret = PTR_ERR(tcu->map);
		goto err_iounmap;
	}

	tcu->clk = of_clk_get_by_name(np, "tcu");
	if (IS_ERR(tcu->clk)) {
		ret = PTR_ERR(tcu->clk);
		pr_crit("ingenic-tcu: Unable to find TCU clock: %i\n", ret);
		goto err_free_regmap;
	}

	ret = clk_prepare_enable(tcu->clk);
	if (ret) {
		pr_crit("ingenic-tcu: Unable to enable TCU clock: %i\n", ret);
		goto err_clk_put;
	}

	ret = ingenic_tcu_intc_init(tcu, np);
	if (ret)
		goto err_clk_disable;

	ret = ingenic_tcu_clk_init(tcu, np);
	if (ret)
		goto err_tcu_intc_cleanup;

	cs_node = of_find_compatible_node(np, NULL,
				"ingenic,jz4740-tcu-clocksource");
	if (of_device_is_available(cs_node)) {
		ret = ingenic_tcu_get_tcu_channel(tcu, cs_node, &res);
		if (ret < 0)
			goto err_tcu_clk_cleanup;

		tcu->cs_channel = ret;

		ret = ingenic_tcu_clocksource_init(tcu, cs_node);
		if (ret)
			goto err_tcu_clk_cleanup;
	}

	timer_node = of_find_compatible_node(np, NULL,
				"ingenic,jz4740-tcu-timer");
	if (of_device_is_available(timer_node)) {
		ret = ingenic_tcu_get_tcu_channel(tcu, timer_node, &res);
		if (ret < 0)
			goto err_tcu_clocksource_cleanup;

		tcu->timer_channel = ret;

		ret = ingenic_tcu_timer_init(tcu, timer_node);
		if (ret)
			goto err_tcu_clocksource_cleanup;
	}


	return 0;

err_tcu_clocksource_cleanup:
	ingenic_tcu_clocksource_cleanup(tcu);
err_tcu_clk_cleanup:
	ingenic_tcu_clk_cleanup(tcu, np);
err_tcu_intc_cleanup:
	ingenic_tcu_intc_cleanup(tcu);
err_clk_disable:
	clk_disable_unprepare(tcu->clk);
err_clk_put:
	clk_put(tcu->clk);
err_free_regmap:
	regmap_exit(tcu->map);
err_iounmap:
	iounmap(base);
	release_mem_region(res.start, resource_size(&res));
err_free_ingenic_tcu:
	kfree(tcu);
	return ret;
}

TIMER_OF_DECLARE(jz4740_tcu_intc,  "ingenic,jz4740-tcu",  ingenic_tcu_init);
TIMER_OF_DECLARE(jz4725b_tcu_intc, "ingenic,jz4725b-tcu", ingenic_tcu_init);
TIMER_OF_DECLARE(jz4770_tcu_intc,  "ingenic,jz4770-tcu",  ingenic_tcu_init);


static int __init ingenic_tcu_probe(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, ingenic_tcu);

	regmap_attach_dev(&pdev->dev, ingenic_tcu->map,
			  &ingenic_tcu_regmap_config);

	return devm_of_platform_populate(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static int ingenic_tcu_suspend(struct device *dev)
{
	struct ingenic_tcu *tcu = dev_get_drvdata(dev);

	clk_disable(tcu->cs_clk);
	clk_disable(tcu->timer_clk);
	clk_disable(tcu->clk);
	return 0;
}

static int ingenic_tcu_resume(struct device *dev)
{
	struct ingenic_tcu *tcu = dev_get_drvdata(dev);
	int ret;

	ret = clk_enable(tcu->clk);
	if (ret)
		return ret;

	ret = clk_enable(tcu->timer_clk);
	if (ret)
		goto err_tcu_clk_disable;

	ret = clk_enable(tcu->cs_clk);
	if (ret)
		goto err_tcu_timer_clk_disable;

	return 0;

err_tcu_timer_clk_disable:
	clk_disable(tcu->timer_clk);
err_tcu_clk_disable:
	clk_disable(tcu->clk);
	return ret;
}

static const struct dev_pm_ops ingenic_tcu_pm_ops = {
	/* _noirq: We want the TCU clock to be gated last / ungated first */
	.suspend_noirq = ingenic_tcu_suspend,
	.resume_noirq  = ingenic_tcu_resume,
};
#define INGENIC_TCU_PM_OPS (&ingenic_tcu_pm_ops)

#else
#define INGENIC_TCU_PM_OPS NULL
#endif /* CONFIG_PM_SUSPEND */

static struct platform_driver ingenic_tcu_driver = {
	.driver = {
		.name	= "ingenic-tcu",
		.pm	= INGENIC_TCU_PM_OPS,
		.of_match_table = ingenic_tcu_of_match,
	},
};
builtin_platform_driver_probe(ingenic_tcu_driver, ingenic_tcu_probe);
