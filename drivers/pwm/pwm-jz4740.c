/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform PWM support
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

#define NUM_PWM 8

struct jz4740_pwm_chip {
	struct pwm_chip chip;
	struct clk *clks[NUM_PWM];
	struct regmap *map;
	struct resource *parent_res;
};

static inline struct jz4740_pwm_chip *to_jz4740(struct pwm_chip *chip)
{
	return container_of(chip, struct jz4740_pwm_chip, chip);
}

static bool jz4740_pwm_can_use_chn(struct jz4740_pwm_chip *jz, unsigned int chn)
{
	struct platform_device *pdev = to_platform_device(jz->chip.dev);
	struct resource chn_res, *res;
	unsigned int i;

	chn_res.start = jz->parent_res->start + TCU_REG_TDFRc(chn);
	chn_res.end = chn_res.start + TCU_CHANNEL_STRIDE - 1;
	chn_res.flags = IORESOURCE_MEM;

	/*
	 * Walk the list of resources, find if there's one that contains the
	 * registers for the requested TCU channel
	 */
	for (i = 0; ; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			break;
		if (resource_contains(res, &chn_res))
			return true;
	}

	return false;
}

static int jz4740_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);
	struct clk *clk;
	char clk_name[16];
	int ret;

	if (!jz4740_pwm_can_use_chn(jz, pwm->hwpwm))
		return -EBUSY;

	snprintf(clk_name, sizeof(clk_name), "timer%u", pwm->hwpwm);

	clk = clk_get(chip->dev, clk_name);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = clk_prepare_enable(clk);
	if (ret) {
		clk_put(clk);
		return ret;
	}

	jz->clks[pwm->hwpwm] = clk;
	return 0;
}

static void jz4740_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);
	struct clk *clk = jz->clks[pwm->hwpwm];

	clk_disable_unprepare(clk);
	clk_put(clk);
}

static int jz4740_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	/* Enable PWM output */
	regmap_update_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
				TCU_TCSR_PWM_EN, TCU_TCSR_PWM_EN);

	/* Start counter */
	regmap_write(jz->map, TCU_REG_TESR, BIT(pwm->hwpwm));
	return 0;
}

static void jz4740_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	/* Disable PWM output.
	 * In TCU2 mode (channel 1/2 on JZ4750+), this must be done before the
	 * counter is stopped, while in TCU1 mode the order does not matter.
	 */
	regmap_update_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
				TCU_TCSR_PWM_EN, 0);

	/* Stop counter */
	regmap_write(jz->map, TCU_REG_TECR, BIT(pwm->hwpwm));
}

static int jz4740_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			     int duty_ns, int period_ns)
{
	struct jz4740_pwm_chip *jz4740 = to_jz4740(pwm->chip);
	struct clk *clk = jz4740->clks[pwm->hwpwm];
	unsigned long rate, new_rate, period, duty;
	unsigned long long tmp;
	unsigned int tcsr;
	bool is_enabled;

	rate = clk_get_rate(clk);

	for (;;) {
		tmp = (unsigned long long) rate * period_ns;
		do_div(tmp, 1000000000);

		if (tmp <= 0xffff)
			break;

		new_rate = clk_round_rate(clk, rate / 2);

		if (new_rate < rate)
			rate = new_rate;
		else
			return -EINVAL;
	}

	clk_set_rate(clk, rate);

	period = tmp;

	tmp = (unsigned long long)period * duty_ns;
	do_div(tmp, period_ns);
	duty = period - tmp;

	if (duty >= period)
		duty = period - 1;

	regmap_read(jz4740->map, TCU_REG_TER, &tcsr);
	is_enabled = tcsr & BIT(pwm->hwpwm);
	if (is_enabled)
		jz4740_pwm_disable(chip, pwm);

	/* Set abrupt shutdown */
	regmap_update_bits(jz4740->map, TCU_REG_TCSRc(pwm->hwpwm),
				TCU_TCSR_PWM_SD, TCU_TCSR_PWM_SD);

	/* Reset counter to 0 */
	regmap_write(jz4740->map, TCU_REG_TCNTc(pwm->hwpwm), 0);

	/* Set duty */
	regmap_write(jz4740->map, TCU_REG_TDHRc(pwm->hwpwm), duty);

	/* Set period */
	regmap_write(jz4740->map, TCU_REG_TDFRc(pwm->hwpwm), period);

	if (is_enabled)
		jz4740_pwm_enable(chip, pwm);

	return 0;
}

static int jz4740_pwm_set_polarity(struct pwm_chip *chip,
		struct pwm_device *pwm, enum pwm_polarity polarity)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	switch (polarity) {
	case PWM_POLARITY_NORMAL:
		regmap_update_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
				TCU_TCSR_PWM_INITL_HIGH, 0);
		break;
	case PWM_POLARITY_INVERSED:
		regmap_update_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
			TCU_TCSR_PWM_INITL_HIGH, TCU_TCSR_PWM_INITL_HIGH);
		break;
	}

	return 0;
}

static const struct pwm_ops jz4740_pwm_ops = {
	.request = jz4740_pwm_request,
	.free = jz4740_pwm_free,
	.config = jz4740_pwm_config,
	.set_polarity = jz4740_pwm_set_polarity,
	.enable = jz4740_pwm_enable,
	.disable = jz4740_pwm_disable,
	.owner = THIS_MODULE,
};

static int jz4740_pwm_probe(struct platform_device *pdev)
{
	struct jz4740_pwm_chip *jz4740;
	struct device *dev = &pdev->dev;

	jz4740 = devm_kzalloc(dev, sizeof(*jz4740), GFP_KERNEL);
	if (!jz4740)
		return -ENOMEM;

	jz4740->map = dev_get_regmap(dev->parent, NULL);
	if (!jz4740->map) {
		dev_err(dev, "regmap not found\n");
		return -EINVAL;
	}

	jz4740->parent_res = platform_get_resource(
				to_platform_device(dev->parent),
				IORESOURCE_MEM, 0);
	if (!jz4740->parent_res)
		return -EINVAL;

	jz4740->chip.dev = dev;
	jz4740->chip.ops = &jz4740_pwm_ops;
	jz4740->chip.npwm = NUM_PWM;
	jz4740->chip.base = -1;
	jz4740->chip.of_xlate = of_pwm_xlate_with_flags;
	jz4740->chip.of_pwm_n_cells = 3;

	platform_set_drvdata(pdev, jz4740);

	return pwmchip_add(&jz4740->chip);
}

static int jz4740_pwm_remove(struct platform_device *pdev)
{
	struct jz4740_pwm_chip *jz4740 = platform_get_drvdata(pdev);

	return pwmchip_remove(&jz4740->chip);
}

#ifdef CONFIG_OF
static const struct of_device_id jz4740_pwm_dt_ids[] = {
	{ .compatible = "ingenic,jz4740-pwm", },
	{ .compatible = "ingenic,jz4770-pwm", },
	{ .compatible = "ingenic,jz4780-pwm", },
	{},
};
MODULE_DEVICE_TABLE(of, jz4740_pwm_dt_ids);
#endif

static struct platform_driver jz4740_pwm_driver = {
	.driver = {
		.name = "jz4740-pwm",
		.of_match_table = of_match_ptr(jz4740_pwm_dt_ids),
	},
	.probe = jz4740_pwm_probe,
	.remove = jz4740_pwm_remove,
};
module_platform_driver(jz4740_pwm_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Ingenic JZ4740 PWM driver");
MODULE_ALIAS("platform:jz4740-pwm");
MODULE_LICENSE("GPL");
