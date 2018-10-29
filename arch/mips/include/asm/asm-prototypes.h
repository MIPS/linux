/*
 * Copyright (C) 2017 Imagination Technologies
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/compiler.h>
#include <linux/types.h>

#include <asm/checksum.h>
#include <asm/fpu.h>
#include <asm/msa.h>
#include <asm/page.h>
#include <asm/string.h>
#include <asm/uaccess.h>

/* Note that these actually place their return values in a2/$6... */
extern __kernel_size_t __bzero(void __user *, long, __kernel_size_t);
extern __kernel_size_t __bzero_kernel(void *, long, __kernel_size_t);

extern long __strlen_kernel_asm(const char *);
extern long __strlen_user_asm(const char __user *);

extern long __strncpy_from_kernel_asm(char *, const char *, long);
extern long __strncpy_from_kernel_nocheck_asm(char *, const char *, long);
extern long __strncpy_from_user_asm(char *, const char __user *, long);
extern long __strncpy_from_user_nocheck_asm(char *, const char __user *, long);

extern long __strnlen_kernel_asm(const char *, long);
extern long __strnlen_kernel_nocheck_asm(const char *, long);
extern long __strnlen_user_asm(const char __user *, long);
extern long __strnlen_user_nocheck_asm(const char __user *, long);
