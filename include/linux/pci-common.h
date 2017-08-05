/**
 * Common PCI definitions
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 */

#ifndef __LINUX_PCI_COMMON_H__
#define __LINUX_PCI_COMMON_H__

/**
 * enum pci_interrupt_pin - PCI INTx interrupt values
 * @PCI_INTERRUPT_UNKNOWN: Unknown or unassigned interrupt
 * @PCI_INTERRUPT_INTA: PCI INTA pin
 * @PCI_INTERRUPT_INTB: PCI INTB pin
 * @PCI_INTERRUPT_INTC: PCI INTC pin
 * @PCI_INTERRUPT_INTD: PCI INTD pin
 *
 * Corresponds to values for legacy PCI INTx interrupts, as can be found in the
 * PCI_INTERRUPT_PIN register.
 */
enum pci_interrupt_pin {
	PCI_INTERRUPT_UNKNOWN,
	PCI_INTERRUPT_INTA,
	PCI_INTERRUPT_INTB,
	PCI_INTERRUPT_INTC,
	PCI_INTERRUPT_INTD,
};

#endif /* __LINUX_PCI_COMMON_H__ */
