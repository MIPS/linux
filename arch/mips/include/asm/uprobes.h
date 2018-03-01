/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_UPROBES_H
#define __ASM_UPROBES_H

#include <linux/notifier.h>
#include <linux/types.h>

#include <asm/break.h>
#include <asm/inst.h>
#include <asm/mipsregs.h>

/*
 * We want this to be defined as union mips_instruction but that makes the
 * generic code blow up.
 */
#ifdef __nanomips__
typedef struct { u16 h[3]; } uprobe_opcode_t;

static inline bool uprobe_opcode_equal(uprobe_opcode_t a, uprobe_opcode_t b)
{
	unsigned int i;

	for (i = 0; i < (nanomips_insn_len(a.h[0]) / 2); i++) {
		if (a.h[i] != b.h[i])
			return false;
	}

	return true;
}
# define uprobe_opcode_equal uprobe_opcode_equal

# define UPROBE_MAX_XOL_INSNS 1

# define UPROBE_XOLBREAK_INSN		((uprobe_opcode_t){{ 0x1014 }})	/* break 4 */
# define UPROBE_SWBP_INSN		((uprobe_opcode_t){{ 0x1013 }})	/* break 3 */
# define UPROBE_SWBP_INSN_SIZE		2
#else
typedef u32 uprobe_opcode_t;
# define UPROBE_MAX_XOL_INSNS 2

# define UPROBE_XOLBREAK_INSN		0x0004000d	/* break 4 */
# define UPROBE_SWBP_INSN		0x0003000d	/* break 3 */
# define UPROBE_SWBP_INSN_SIZE		4
#endif

#define UPROBE_XOL_SLOT_BYTES		128	/* Max. cache line size */

struct arch_uprobe {
	unsigned long	resume_epc;
	uprobe_opcode_t	insn[UPROBE_MAX_XOL_INSNS];
	uprobe_opcode_t	ixol[2];
};

struct arch_uprobe_task {
	unsigned long saved_trap_nr;
};

#endif /* __ASM_UPROBES_H */
