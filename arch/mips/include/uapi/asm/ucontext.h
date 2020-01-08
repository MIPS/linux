/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __MIPS_UAPI_ASM_UCONTEXT_H
#define __MIPS_UAPI_ASM_UCONTEXT_H

#include <asm/byteorder.h>

/**
 * struct extcontext - extended context header structure
 * @magic:	magic value identifying the type of extended context
 * @size:	the size in bytes of the enclosing structure
 *
 * Extended context structures provide context which does not fit within struct
 * sigcontext. They are placed sequentially in memory at the end of struct
 * ucontext and struct sigframe, with each extended context structure beginning
 * with a header defined by this struct. The type of context represented is
 * indicated by the magic field. Userland may check each extended context
 * structure against magic values that it recognises. The size field allows any
 * unrecognised context to be skipped, allowing for future expansion. The end
 * of the extended context data is indicated by the magic value
 * END_EXTCONTEXT_MAGIC.
 */
struct extcontext {
	unsigned int		magic;
	unsigned int		size;
};

/**
 * struct msa_extcontext - MSA extended context structure
 * @ext:	the extended context header, with magic == MSA_EXTCONTEXT_MAGIC
 * @wr:		the most significant 64 bits of each MSA vector register
 * @csr:	the value of the MSA control & status register
 *
 * If MSA context is live for a task at the time a signal is delivered to it,
 * this structure will hold the MSA context of the task as it was prior to the
 * signal delivery.
 *
 * On nanoMIPS ABIs the most significant 64 bits of the MSA vector registers can
 * be found in the fpu_extcontext entry with the rest of the vector register
 * state. Use the FPU_EXTCTXT_RA() macro to abstract endianness differences.
 */
struct msa_extcontext {
	struct extcontext	ext;
#define MSA_EXTCONTEXT_MAGIC	0x784d5341	/* xMSA */

#if (_MIPS_SIM == _MIPS_SIM_ABI32) || \
    (_MIPS_SIM == _MIPS_SIM_NABI32) || \
    (_MIPS_SIM == _MIPS_SIM_ABI64)
	unsigned long long	wr[32];
#endif /* _MIPS_SIM == _MIPS_SIM_ABI32 or _MIPS_SIM_NABI32 or _MIPS_SIM_ABI64 */
	unsigned int		csr;
};

#if 0	/* TODO implement nanoMIPS FP/MSA extcontext support */
#if _MIPS_SIM == _MIPS_SIM_PABI32

/**
 * struct dsp_extcontext - nanoMIPS DSP extended context structure
 * @ext:	the extended context header, with magic == DSP_EXTCONTEXT_MAGIC
 * @hi:		the values of the hi accumulators
 * @lo:		the values of the lo accumulators
 *
 * If DSP context is live for a task at the time a signal is delivered to it,
 * this structure will hold the DSP context of the task as it was prior to the
 * signal delivery.
 */
struct dsp_extcontext {
	struct extcontext	ext;
# define DSP_EXTCONTEXT_MAGIC	0x78445350	/* xDSP */

	unsigned long		hi[4];
	unsigned long		lo[4];
	__u32			dsp;
};

/**
 * struct fpu_extcontext - nanoMIPS FPU / Vector extended context structure
 * @ext:	the extended context header, with magic == FPU_EXTCONTEXT_MAGIC
 * @fcsr:	the value of the FPU context & status register
 * @width:	width of each register in bytes, always a power of two
 * @r:		the FPU registers, each one a full native-endian wide value.
 *		use FPU_EXTCTXT_RA() macro after validating @width to get the
 *		address of an element of a register, or an FPU register since it
 *		may not be at the low address.
 *
 * If FPU or vector context is live for a task at the time a signal is delivered
 * to it, this structure will hold the FPU context of the task as it was prior
 * to the signal delivery, including any vector register state which aliases the
 * FPU registers.
 */
struct fpu_extcontext {
	struct extcontext	ext;
# define FPU_EXTCONTEXT_MAGIC	0x78465055	/* xFPU */

	unsigned int		fcsr;
	unsigned int		width;

	unsigned long long	r[0];
};

# ifdef __MIPSEL__
#  define FPU_EXTCTXT_OFFSET(idx, width, type)	((idx) * (size))
# endif /* __MIPSEL__ */

# ifdef __MIPSEB__
#  define FPU_EXTCTXT_OFFSET(idx, width, type)	(((idx) * (size)) ^	\
						 ((width) - (size)))
# endif /* __MIPSEB__ */

/**
 * FPU_EXTCTX_RA() - Get address of vector reg element in fpu_extcontext
 * @fpuec:	pointer to struct fpu_extcontext entry
 * @reg:	FPU or vector register number
 * @type:	type of element to return address of
 * @idx:	index of element within vector register (0 for FPU register)
 *
 * Abstract away endianness differences to find a pointer to a single element in
 * a vector register. Due to the little endian aliasing of different sized
 * elements in the MSA vector registers, it is unsafe to simply index the
 * resulting pointer, as that will do the wrong thing on big endian CPUs.
 *
 * E.g.:
 *	float f24_sgl = *FPU_EXTCTXT_RA(&fpuec, 24, float, 0)
 *	double f30_dbl = *FPU_EXTCTXT_RA(&fpuec, 30, double, 0)
 *	u64 w4_hi = *FPU_EXTCTXT_RA(&fpuec, 4, u64, 1)
 */
# define FPU_EXTCTXT_RA(fpuec, reg, type, idx) ({			\
	struct fpu_extcontext *_fpuec = (fpuec);			\
	unsigned long _size = sizeof(type);				\
	(type *)(unsigned long)_fpuec->r + (reg) * _fpuec->width	\
		 + FPU_EXTCTX_OFFSET(idx, _fpuec->width, _size);	\
})

#endif /* _MIPS_SIM == _MIPS_SIM_PABI32 */
#endif

#define END_EXTCONTEXT_MAGIC	0x78454e44	/* xEND */

/**
 * struct ucontext - user context structure
 * @uc_flags:
 * @uc_link:
 * @uc_stack:
 * @uc_mcontext:	holds basic processor state
 * @uc_sigmask:
 * @uc_extcontext:	holds extended processor state
 */
struct ucontext {
	/* Historic fields matching asm-generic */
	unsigned long		uc_flags;
	struct ucontext		*uc_link;
	stack_t			uc_stack;
	struct sigcontext	uc_mcontext;
	sigset_t		uc_sigmask;

	/* Extended context structures may follow ucontext */
	unsigned long long	uc_extcontext[0];
};

#endif /* __MIPS_UAPI_ASM_UCONTEXT_H */
