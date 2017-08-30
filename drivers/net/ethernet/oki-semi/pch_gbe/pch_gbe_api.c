/*
 * Copyright (C) 1999 - 2010 Intel Corporation.
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
 *
 * This code was derived from the Intel e1000e Linux driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "pch_gbe.h"
#include "pch_gbe_phy.h"
#include "pch_gbe_api.h"

static const struct pch_gbe_functions pch_gbe_ops = {
	.reset_phy         = pch_gbe_phy_hw_reset,
	.sw_reset_phy      = pch_gbe_phy_sw_reset,
	.power_up_phy      = pch_gbe_phy_power_up,
	.power_down_phy    = pch_gbe_phy_power_down,
};

/**
 * pch_gbe_plat_init_function_pointers - Init func ptrs
 * @hw:	Pointer to the HW structure
 */
static void pch_gbe_plat_init_function_pointers(struct pch_gbe_hw *hw)
{
	/* Set PHY parameter */
	hw->phy.reset_delay_us     = PCH_GBE_PHY_RESET_DELAY_US;
	/* Set function pointers */
	hw->func = &pch_gbe_ops;
}

/**
 * pch_gbe_hal_setup_init_funcs - Initializes function pointers
 * @hw:	Pointer to the HW structure
 * Returns:
 *	0:	Successfully
 *	ENOSYS:	Function is not registered
 */
s32 pch_gbe_hal_setup_init_funcs(struct pch_gbe_hw *hw)
{
	if (!hw->reg) {
		struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);

		netdev_err(adapter->netdev, "ERROR: Registers not mapped\n");
		return -ENOSYS;
	}
	pch_gbe_plat_init_function_pointers(hw);
	return 0;
}

/**
 * pch_gbe_hal_phy_hw_reset - Hard PHY reset
 * @hw:	    Pointer to the HW structure
 */
void pch_gbe_hal_phy_hw_reset(struct pch_gbe_hw *hw)
{
	if (!hw->func->reset_phy) {
		struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);

		netdev_err(adapter->netdev, "ERROR: configuration\n");
		return;
	}
	hw->func->reset_phy(hw);
}

/**
 * pch_gbe_hal_phy_sw_reset - Soft PHY reset
 * @hw:	    Pointer to the HW structure
 */
void pch_gbe_hal_phy_sw_reset(struct pch_gbe_hw *hw)
{
	if (!hw->func->sw_reset_phy) {
		struct pch_gbe_adapter *adapter = pch_gbe_hw_to_adapter(hw);

		netdev_err(adapter->netdev, "ERROR: configuration\n");
		return;
	}
	hw->func->sw_reset_phy(hw);
}

/**
 * pch_gbe_hal_power_up_phy - Power up PHY
 * @hw:	Pointer to the HW structure
 */
void pch_gbe_hal_power_up_phy(struct pch_gbe_hw *hw)
{
	if (hw->func->power_up_phy)
		hw->func->power_up_phy(hw);
}

/**
 * pch_gbe_hal_power_down_phy - Power down PHY
 * @hw:	Pointer to the HW structure
 */
void pch_gbe_hal_power_down_phy(struct pch_gbe_hw *hw)
{
	if (hw->func->power_down_phy)
		hw->func->power_down_phy(hw);
}
