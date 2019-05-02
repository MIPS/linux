/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DRIVERS_CLOCKSOURCE_INGENIC_TIMER_H__
#define __DRIVERS_CLOCKSOURCE_INGENIC_TIMER_H__

#include <linux/compiler_types.h>

/*
 * README: For use *ONLY* by the ingenic-ost driver.
 * Regular drivers which want to access the TCU registers
 * must have ingenic-timer as parent and retrieve the regmap
 * doing dev_get_regmap(pdev->dev.parent);
 */
extern void __iomem *ingenic_tcu_base;

#endif /* __DRIVERS_CLOCKSOURCE_INGENIC_TIMER_H__ */
