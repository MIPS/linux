/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "vdso.h"

#include <uapi/asm/unistd.h>

#include <asm/regdef.h>
#include <asm/asm.h>

	.section	.text
	.cfi_sections	.debug_frame

LEAF(__vdso_rt_sigreturn)
	.cfi_signal_frame

	li	scn, __NR_rt_sigreturn
#if __nanomips__
	syscall32
#else
	syscall
#endif

	END(__vdso_rt_sigreturn)

#if _MIPS_SIM == _MIPS_SIM_ABI32

LEAF(__vdso_sigreturn)
	.cfi_signal_frame

	li	scn, __NR_sigreturn
	syscall

	END(__vdso_sigreturn)

#endif
