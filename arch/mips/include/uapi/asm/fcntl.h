/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2003, 05 Ralf Baechle
 */
#ifndef _UAPI_ASM_FCNTL_H
#define _UAPI_ASM_FCNTL_H

#if (_MIPS_SIM == _MIPS_SIM_ABI32) || \
    (_MIPS_SIM == _MIPS_SIM_NABI32) || \
    (_MIPS_SIM == _MIPS_SIM_ABI64)

#include <asm/sgidefs.h>

#define O_APPEND	0x0008
#define O_DSYNC		0x0010	/* used to be O_SYNC, see below */
#define O_NONBLOCK	0x0080
#define O_CREAT		0x0100	/* not fcntl */
#define O_TRUNC		0x0200	/* not fcntl */
#define O_EXCL		0x0400	/* not fcntl */
#define O_NOCTTY	0x0800	/* not fcntl */
#define FASYNC		0x1000	/* fcntl, for BSD compatibility */
#define O_LARGEFILE	0x2000	/* allow large file opens */
/*
 * Before Linux 2.6.33 only O_DSYNC semantics were implemented, but using
 * the O_SYNC flag.  We continue to use the existing numerical value
 * for O_DSYNC semantics now, but using the correct symbolic name for it.
 * This new value is used to request true Posix O_SYNC semantics.  It is
 * defined in this strange way to make sure applications compiled against
 * new headers get at least O_DSYNC semantics on older kernels.
 *
 * This has the nice side-effect that we can simply test for O_DSYNC
 * wherever we do not care if O_DSYNC or O_SYNC is used.
 *
 * Note: __O_SYNC must never be used directly.
 */
#define __O_SYNC	0x4000
#define O_SYNC		(__O_SYNC|O_DSYNC)
#define O_DIRECT	0x8000	/* direct disk access hint */

#define F_GETLK		14
#define F_SETLK		6
#define F_SETLKW	7

#define F_SETOWN	24	/*  for sockets. */
#define F_GETOWN	23	/*  for sockets. */

#ifndef __mips64
#define F_GETLK64	33	/*  using 'struct flock64' */
#define F_SETLK64	34
#define F_SETLKW64	35
#endif

/*
 * The flavours of struct flock.  "struct flock" is the ABI compliant
 * variant.  Finally struct flock64 is the LFS variant of struct flock.	 As
 * a historic accident and inconsistence with the ABI definition it doesn't
 * contain all the same fields as struct flock.
 */

#if _MIPS_SIM != _MIPS_SIM_ABI64

#include <linux/types.h>

struct flock {
	short	l_type;
	short	l_whence;
	__kernel_off_t	l_start;
	__kernel_off_t	l_len;
	long	l_sysid;
	__kernel_pid_t l_pid;
	long	pad[4];
};

#define HAVE_ARCH_STRUCT_FLOCK

#endif /* _MIPS_SIM == _MIPS_SIM_ABI32 */

#endif /* _MIPS_SIM == _MIPS_SIM_ABI32, _MIPS_SIM_NABI32, or _MIPS_SIM_ABI64 */

#include <asm-generic/fcntl.h>

#endif /* _UAPI_ASM_FCNTL_H */
