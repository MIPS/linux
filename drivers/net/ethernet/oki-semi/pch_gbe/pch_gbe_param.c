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
#include <linux/module.h>	/* for __MODULE_STRING */

#define OPTION_UNSET   -1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

/**
 * TxDescriptors - Transmit Descriptor Count
 * @Valid Range:   PCH_GBE_MIN_TXD - PCH_GBE_MAX_TXD
 * @Default Value: PCH_GBE_DEFAULT_TXD
 */
static int TxDescriptors = OPTION_UNSET;
module_param(TxDescriptors, int, 0);
MODULE_PARM_DESC(TxDescriptors, "Number of transmit descriptors");

/**
 * RxDescriptors -Receive Descriptor Count
 * @Valid Range:   PCH_GBE_MIN_RXD - PCH_GBE_MAX_RXD
 * @Default Value: PCH_GBE_DEFAULT_RXD
 */
static int RxDescriptors = OPTION_UNSET;
module_param(RxDescriptors, int, 0);
MODULE_PARM_DESC(RxDescriptors, "Number of receive descriptors");

/**
 * FlowControl - User Specified Flow Control Override
 * @Valid Range: 0-3
 *    - 0:  No Flow Control
 *    - 1:  Rx only, respond to PAUSE frames but do not generate them
 *    - 2:  Tx only, generate PAUSE frames but ignore them on receive
 *    - 3:  Full Flow Control Support
 * @Default Value: Read flow control settings from the EEPROM
 */
static int FlowControl = OPTION_UNSET;
module_param(FlowControl, int, 0);
MODULE_PARM_DESC(FlowControl, "Flow Control setting");

/*
 * XsumRX - Receive Checksum Offload Enable/Disable
 * @Valid Range: 0, 1
 *    - 0:  disables all checksum offload
 *    - 1:  enables receive IP/TCP/UDP checksum offload
 * @Default Value: PCH_GBE_DEFAULT_RX_CSUM
 */
static int XsumRX = OPTION_UNSET;
module_param(XsumRX, int, 0);
MODULE_PARM_DESC(XsumRX, "Disable or enable Receive Checksum offload");

#define PCH_GBE_DEFAULT_RX_CSUM             true	/* trueorfalse */

/*
 * XsumTX - Transmit Checksum Offload Enable/Disable
 * @Valid Range: 0, 1
 *    - 0:  disables all checksum offload
 *    - 1:  enables transmit IP/TCP/UDP checksum offload
 * @Default Value: PCH_GBE_DEFAULT_TX_CSUM
 */
static int XsumTX = OPTION_UNSET;
module_param(XsumTX, int, 0);
MODULE_PARM_DESC(XsumTX, "Disable or enable Transmit Checksum offload");

#define PCH_GBE_DEFAULT_TX_CSUM             true	/* trueorfalse */

/**
 * pch_gbe_option - Force the MAC's flow control settings
 * @hw:	            Pointer to the HW structure
 * Returns:
 *	0:			Successful.
 *	Negative value:		Failed.
 */
struct pch_gbe_option {
	enum { enable_option, range_option, list_option } type;
	char *name;
	char *err;
	int  def;
	union {
		struct { /* range_option info */
			int min;
			int max;
		} r;
		struct { /* list_option info */
			int nr;
			const struct pch_gbe_opt_list { int i; char *str; } *p;
		} l;
	} arg;
};

static const struct pch_gbe_opt_list fc_list[] = {
	{ PCH_GBE_FC_NONE, "Flow Control Disabled" },
	{ PCH_GBE_FC_RX_PAUSE, "Flow Control Receive Only" },
	{ PCH_GBE_FC_TX_PAUSE, "Flow Control Transmit Only" },
	{ PCH_GBE_FC_FULL, "Flow Control Enabled" }
};

/**
 * pch_gbe_validate_option - Validate option
 * @value:    value
 * @opt:      option
 * @adapter:  Board private structure
 * Returns:
 *	0:			Successful.
 *	Negative value:		Failed.
 */
static int pch_gbe_validate_option(int *value,
				    const struct pch_gbe_option *opt,
				    struct pch_gbe_adapter *adapter)
{
	if (*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			netdev_dbg(adapter->netdev, "%s Enabled\n", opt->name);
			return 0;
		case OPTION_DISABLED:
			netdev_dbg(adapter->netdev, "%s Disabled\n", opt->name);
			return 0;
		}
		break;
	case range_option:
		if (*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			netdev_dbg(adapter->netdev, "%s set to %i\n",
				   opt->name, *value);
			return 0;
		}
		break;
	case list_option: {
		int i;
		const struct pch_gbe_opt_list *ent;

		for (i = 0; i < opt->arg.l.nr; i++) {
			ent = &opt->arg.l.p[i];
			if (*value == ent->i) {
				if (ent->str[0] != '\0')
					netdev_dbg(adapter->netdev, "%s\n",
						   ent->str);
				return 0;
			}
		}
	}
		break;
	default:
		BUG();
	}

	netdev_dbg(adapter->netdev, "Invalid %s value specified (%i) %s\n",
		   opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

/**
 * pch_gbe_check_options - Range Checking for Command Line Parameters
 * @adapter:  Board private structure
 */
void pch_gbe_check_options(struct pch_gbe_adapter *adapter)
{
	struct pch_gbe_hw *hw = &adapter->hw;
	struct net_device *dev = adapter->netdev;
	int val;

	{ /* Transmit Descriptor Count */
		static const struct pch_gbe_option opt = {
			.type = range_option,
			.name = "Transmit Descriptors",
			.err  = "using default of "
				__MODULE_STRING(PCH_GBE_DEFAULT_TXD),
			.def  = PCH_GBE_DEFAULT_TXD,
			.arg  = { .r = { .min = PCH_GBE_MIN_TXD,
					 .max = PCH_GBE_MAX_TXD } }
		};
		struct pch_gbe_tx_ring *tx_ring = adapter->tx_ring;
		tx_ring->count = TxDescriptors;
		pch_gbe_validate_option(&tx_ring->count, &opt, adapter);
		tx_ring->count = roundup(tx_ring->count,
					PCH_GBE_TX_DESC_MULTIPLE);
	}
	{ /* Receive Descriptor Count */
		static const struct pch_gbe_option opt = {
			.type = range_option,
			.name = "Receive Descriptors",
			.err  = "using default of "
				__MODULE_STRING(PCH_GBE_DEFAULT_RXD),
			.def  = PCH_GBE_DEFAULT_RXD,
			.arg  = { .r = { .min = PCH_GBE_MIN_RXD,
					 .max = PCH_GBE_MAX_RXD } }
		};
		struct pch_gbe_rx_ring *rx_ring = adapter->rx_ring;
		rx_ring->count = RxDescriptors;
		pch_gbe_validate_option(&rx_ring->count, &opt, adapter);
		rx_ring->count = roundup(rx_ring->count,
				PCH_GBE_RX_DESC_MULTIPLE);
	}
	{ /* Checksum Offload Enable/Disable */
		static const struct pch_gbe_option opt = {
			.type = enable_option,
			.name = "Checksum Offload",
			.err  = "defaulting to Enabled",
			.def  = PCH_GBE_DEFAULT_RX_CSUM
		};
		val = XsumRX;
		pch_gbe_validate_option(&val, &opt, adapter);
		if (!val)
			dev->features &= ~NETIF_F_RXCSUM;
	}
	{ /* Checksum Offload Enable/Disable */
		static const struct pch_gbe_option opt = {
			.type = enable_option,
			.name = "Checksum Offload",
			.err  = "defaulting to Enabled",
			.def  = PCH_GBE_DEFAULT_TX_CSUM
		};
		val = XsumTX;
		pch_gbe_validate_option(&val, &opt, adapter);
		if (!val)
			dev->features &= ~NETIF_F_CSUM_MASK;
	}
	{ /* Flow Control */
		static const struct pch_gbe_option opt = {
			.type = list_option,
			.name = "Flow Control",
			.err  = "reading default settings from EEPROM",
			.def  = PCH_GBE_FC_DEFAULT,
			.arg  = { .l = { .nr = (int)ARRAY_SIZE(fc_list),
					 .p = fc_list } }
		};
		int tmp = FlowControl;

		pch_gbe_validate_option(&tmp, &opt, adapter);
		hw->mac.fc = tmp;
	}
}
