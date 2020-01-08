/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999, 2000, 03, 04 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2007  Maciej W. Rozycki
 * Copyright (C) 2014, Imagination Technologies Ltd.
 */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/asm-eva.h>
#include <asm/extable.h>

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */
#ifdef CONFIG_32BIT

#ifdef CONFIG_KVM_GUEST
#define __UA_LIMIT 0xC0000000UL
#else
#define __UA_LIMIT 0x80000000UL
#endif

#define __UA_ADDR	".word"

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT

extern u64 __ua_limit;

#define __UA_LIMIT	__ua_limit

#define __UA_ADDR	".dword"

#endif /* CONFIG_64BIT */

/*
 * USER_DS is a bitmask that has the bits set that may not be set in a valid
 * userspace address.  Note that we limit 32-bit userspace to 0x7fff8000 but
 * the arithmetic we're doing only works if the limit is a power of two, so
 * we use 0x80000000 here on 32-bit kernels.  If a process passes an invalid
 * address in this range it's the process's problem, not ours :-)
 */

#define KERNEL_DS	((mm_segment_t) { 0UL })
#define USER_DS		((mm_segment_t) { __UA_LIMIT })

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current_thread_info()->addr_limit)
#define set_fs(x)	(current_thread_info()->addr_limit = (x))

#define segment_eq(a, b)	((a).seg == (b).seg)

/*
 * eva_kernel_access() - determine whether kernel memory access on an EVA system
 *
 * Determines whether memory accesses should be performed to kernel memory
 * on a system using Extended Virtual Addressing (EVA).
 *
 * Return: true if a kernel memory access on an EVA system, else false.
 */
static inline bool eva_kernel_access(void)
{
	if (!IS_ENABLED(CONFIG_EVA))
		return false;

	return uaccess_kernel();
}

/**
 * eva_user_access() - determine whether access should use EVA instructions
 *
 * Determines whether memory accesses should be performed using EVA memory
 * access instructions - that is, whether to access the user address space on
 * an EVA system.
 *
 * Return: true if user memory access on an EVA system, else false
 */
static inline bool eva_user_access(void)
{
	return IS_ENABLED(CONFIG_EVA) && !eva_kernel_access();
}

/*
 * Is a address valid? This does a straightforward calculation rather
 * than tests.
 *
 * Address valid if:
 *  - "addr" doesn't have any high-bits set
 *  - AND "size" doesn't have any high-bits set
 *  - AND "addr+size" doesn't have any high-bits set
 *  - OR we are in kernel mode.
 *
 * __ua_size() is a trick to avoid runtime checking of positive constant
 * sizes; for those we already know at compile time that the size is ok.
 *
 * __ua_kvm_comm() is to prevent accesses below 32KiB in KVM guest kernels,
 * where there is a risk KVM may have mapped the comm page within easy reach of
 * the zero register.
 */
#define __ua_size(size)							\
	((__builtin_constant_p(size) && (signed long) (size) > 0) ? 0 : (size))

#ifdef CONFIG_KVM_GUEST
#define __ua_kvm_comm(addr)	((addr) - 0x8000)
#else
#define __ua_kvm_comm(addr)	0
#endif

/*
 * access_ok: - Checks if a user space pointer is valid
 * @type: Type of access: %VERIFY_READ or %VERIFY_WRITE.  Note that
 *	  %VERIFY_WRITE is a superset of %VERIFY_READ - if it is safe
 *	  to write to a block, it is always safe to read from it.
 * @addr: User space pointer to start of block to check
 * @size: Size of block to check
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Checks if a pointer to a block of memory in user space is valid.
 *
 * Returns true (nonzero) if the memory block may be valid, false (zero)
 * if it is definitely invalid.
 *
 * Note that, depending on architecture, this function probably just
 * checks that the pointer is in the user space range - after calling
 * this function, memory access functions may still return -EFAULT.
 */

static inline int __access_ok(const void __user *p, unsigned long size)
{
	unsigned long addr = (unsigned long)p;
	return (get_fs().seg & (addr | (addr + size) | __ua_size(size) |
				__ua_kvm_comm(addr))) == 0;
}

#define access_ok(type, addr, size)					\
	likely(__access_ok((addr), (size)))

/*
 * put_user: - Write a simple value into user space.
 * @x:	 Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define put_user(x,ptr) \
	__put_user_check((x), (ptr), sizeof(*(ptr)))

/*
 * get_user: - Get a simple variable from user space.
 * @x:	 Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#define get_user(x,ptr) \
	__get_user_check((x), (ptr), sizeof(*(ptr)))

/*
 * __put_user: - Write a simple value into user space, with less checking.
 * @x:	 Value to copy to user space.
 * @ptr: Destination address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * This macro copies a single simple value from kernel space to user
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and @x must be assignable
 * to the result of dereferencing @ptr.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 */
#define __put_user(x,ptr) \
	__put_user_nocheck((x), (ptr), sizeof(*(ptr)))

/*
 * __get_user: - Get a simple variable from user space, with less checking.
 * @x:	 Variable to store result.
 * @ptr: Source address, in user space.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * This macro copies a single simple variable from user space to kernel
 * space.  It supports simple types like char and int, but not larger
 * data types like structures or arrays.
 *
 * @ptr must have pointer-to-simple-variable type, and the result of
 * dereferencing @ptr must be assignable to @x without a cast.
 *
 * Caller must check the pointer with access_ok() before calling this
 * function.
 *
 * Returns zero on success, or -EFAULT on error.
 * On error, the variable @x is set to zero.
 */
#define __get_user(x,ptr) \
	__get_user_nocheck((x), (ptr), sizeof(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct __user *)(x))

/*
 * Yuck.  We need two variants, one for 64bit operation and one
 * for 32 bit mode and old iron.
 */
#ifndef CONFIG_EVA
#define __get_kernel_common(val, size, ptr) __get_user_common(val, size, ptr)
#else
/*
 * Kernel specific functions for EVA. We need to use normal load instructions
 * to read data from kernel when operating in EVA mode. We use these macros to
 * avoid redefining __get_user_asm for EVA.
 */
#undef _loadd
#undef _loadw
#undef _loadh
#undef _loadb
#ifdef CONFIG_32BIT
#define _loadd			_loadw
#else
#define _loadd(reg, addr)	"ld " reg ", " addr
#endif
#define _loadw(reg, addr)	"lw " reg ", " addr
#define _loadh(reg, addr)	"lh " reg ", " addr
#define _loadb(reg, addr)	"lb " reg ", " addr

#define __get_kernel_common(val, size, ptr)				\
do {									\
	switch (size) {							\
	case 1: __get_data_asm(val, _loadb, ptr); break;		\
	case 2: __get_data_asm(val, _loadh, ptr); break;		\
	case 4: __get_data_asm(val, _loadw, ptr); break;		\
	case 8: __GET_DW(val, _loadd, ptr); break;			\
	default: __get_user_unknown(); break;				\
	}								\
} while (0)
#endif

#ifdef CONFIG_32BIT
#define __GET_DW(val, insn, ptr) __get_data_asm_ll32(val, insn, ptr)
#endif
#ifdef CONFIG_64BIT
#define __GET_DW(val, insn, ptr) __get_data_asm(val, insn, ptr)
#endif

extern void __get_user_unknown(void);

#define __get_user_common(val, size, ptr)				\
do {									\
	switch (size) {							\
	case 1: __get_data_asm(val, user_lb, ptr); break;		\
	case 2: __get_data_asm(val, user_lh, ptr); break;		\
	case 4: __get_data_asm(val, user_lw, ptr); break;		\
	case 8: __GET_DW(val, user_ld, ptr); break;			\
	default: __get_user_unknown(); break;				\
	}								\
} while (0)

#define __get_user_nocheck(x, ptr, size)				\
({									\
	int __gu_err;							\
									\
	if (eva_kernel_access()) {					\
		__get_kernel_common((x), size, ptr);			\
	} else {							\
		__chk_user_ptr(ptr);					\
		__get_user_common((x), size, ptr);			\
	}								\
	__gu_err;							\
})

#define __get_user_check(x, ptr, size)					\
({									\
	int __gu_err = -EFAULT;						\
	const __typeof__(*(ptr)) __user * __gu_ptr = (ptr);		\
									\
	might_fault();							\
	if (likely(access_ok(VERIFY_READ,  __gu_ptr, size))) {		\
		if (eva_kernel_access())				\
			__get_kernel_common((x), size, __gu_ptr);	\
		else							\
			__get_user_common((x), size, __gu_ptr);		\
	} else								\
		(x) = 0;						\
									\
	__gu_err;							\
})

#define __get_data_asm(val, insn, addr)					\
{									\
	long __gu_tmp;							\
									\
	__asm__ __volatile__(						\
	"1:	"insn("%1", "%3")"				\n"	\
	"2:							\n"	\
	"	.insn						\n"	\
	"	.section .fixup,\"ax\"				\n"	\
	"3:	li	%0, %4					\n"	\
	"	move	%1, $zero				\n"	\
	"	j	2b					\n"	\
	"	.previous					\n"	\
	"	.section __ex_table,\"a\"			\n"	\
	"	"__UA_ADDR "\t1b, 3b				\n"	\
	"	.previous					\n"	\
	: "=r" (__gu_err), "=r" (__gu_tmp)				\
	: "0" (0), "o" (__m(addr)), "i" (-EFAULT));			\
									\
	(val) = (__typeof__(*(addr))) __gu_tmp;				\
}

/*
 * Get a long long 64 using 32 bit registers.
 */
#define __get_data_asm_ll32(val, insn, addr)				\
{									\
	union {								\
		unsigned long long	l;				\
		__typeof__(*(addr))	t;				\
	} __gu_tmp;							\
									\
	__asm__ __volatile__(						\
	"1:	" insn("%1", "(%3)")"				\n"	\
	"2:	" insn("%D1", "4(%3)")"				\n"	\
	"3:							\n"	\
	"	.insn						\n"	\
	"	.section	.fixup,\"ax\"			\n"	\
	"4:	li	%0, %4					\n"	\
	"	move	%1, $zero				\n"	\
	"	move	%D1, $zero				\n"	\
	"	j	3b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 4b				\n"	\
	"	" __UA_ADDR "	2b, 4b				\n"	\
	"	.previous					\n"	\
	: "=r" (__gu_err), "=&r" (__gu_tmp.l)				\
	: "0" (0), "r" (addr), "i" (-EFAULT));				\
									\
	(val) = __gu_tmp.t;						\
}

#ifndef CONFIG_EVA
#define __put_kernel_common(ptr, size) __put_user_common(ptr, size)
#else
/*
 * Kernel specific functions for EVA. We need to use normal load instructions
 * to read data from kernel when operating in EVA mode. We use these macros to
 * avoid redefining __get_data_asm for EVA.
 */
#undef _stored
#undef _storew
#undef _storeh
#undef _storeb
#ifdef CONFIG_32BIT
#define _stored			_storew
#else
#define _stored(reg, addr)	"ld " reg ", " addr
#endif

#define _storew(reg, addr)	"sw " reg ", " addr
#define _storeh(reg, addr)	"sh " reg ", " addr
#define _storeb(reg, addr)	"sb " reg ", " addr

#define __put_kernel_common(ptr, size)					\
do {									\
	switch (size) {							\
	case 1: __put_data_asm(_storeb, ptr); break;			\
	case 2: __put_data_asm(_storeh, ptr); break;			\
	case 4: __put_data_asm(_storew, ptr); break;			\
	case 8: __PUT_DW(_stored, ptr); break;				\
	default: __put_user_unknown(); break;				\
	}								\
} while(0)
#endif

/*
 * Yuck.  We need two variants, one for 64bit operation and one
 * for 32 bit mode and old iron.
 */
#ifdef CONFIG_32BIT
#define __PUT_DW(insn, ptr) __put_data_asm_ll32(insn, ptr)
#endif
#ifdef CONFIG_64BIT
#define __PUT_DW(insn, ptr) __put_data_asm(insn, ptr)
#endif

#define __put_user_common(ptr, size)					\
do {									\
	switch (size) {							\
	case 1: __put_data_asm(user_sb, ptr); break;			\
	case 2: __put_data_asm(user_sh, ptr); break;			\
	case 4: __put_data_asm(user_sw, ptr); break;			\
	case 8: __PUT_DW(user_sd, ptr); break;				\
	default: __put_user_unknown(); break;				\
	}								\
} while (0)

#define __put_user_nocheck(x, ptr, size)				\
({									\
	__typeof__(*(ptr)) __pu_val;					\
	int __pu_err = 0;						\
									\
	__pu_val = (x);							\
	if (eva_kernel_access()) {					\
		__put_kernel_common(ptr, size);				\
	} else {							\
		__chk_user_ptr(ptr);					\
		__put_user_common(ptr, size);				\
	}								\
	__pu_err;							\
})

#define __put_user_check(x, ptr, size)					\
({									\
	__typeof__(*(ptr)) __user *__pu_addr = (ptr);			\
	__typeof__(*(ptr)) __pu_val = (x);				\
	int __pu_err = -EFAULT;						\
									\
	might_fault();							\
	if (likely(access_ok(VERIFY_WRITE,  __pu_addr, size))) {	\
		if (eva_kernel_access())				\
			__put_kernel_common(__pu_addr, size);		\
		else							\
			__put_user_common(__pu_addr, size);		\
	}								\
									\
	__pu_err;							\
})

#define __put_data_asm(insn, ptr)					\
{									\
	__asm__ __volatile__(						\
	"1:	"insn("%z2", "%3")"	# __put_data_asm	\n"	\
	"2:							\n"	\
	"	.insn						\n"	\
	"	.section	.fixup,\"ax\"			\n"	\
	"3:	li	%0, %4					\n"	\
	"	j	2b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 3b				\n"	\
	"	.previous					\n"	\
	: "=r" (__pu_err)						\
	: "0" (0), "Jr" (__pu_val), "o" (__m(ptr)),			\
	  "i" (-EFAULT));						\
}

#define __put_data_asm_ll32(insn, ptr)					\
{									\
	__asm__ __volatile__(						\
	"1:	"insn("%2", "(%3)")"	# __put_data_asm_ll32	\n"	\
	"2:	"insn("%D2", "4(%3)")"				\n"	\
	"3:							\n"	\
	"	.insn						\n"	\
	"	.section	.fixup,\"ax\"			\n"	\
	"4:	li	%0, %4					\n"	\
	"	j	3b					\n"	\
	"	.previous					\n"	\
	"	.section	__ex_table,\"a\"		\n"	\
	"	" __UA_ADDR "	1b, 4b				\n"	\
	"	" __UA_ADDR "	2b, 4b				\n"	\
	"	.previous"						\
	: "=r" (__pu_err)						\
	: "0" (0), "r" (__pu_val), "r" (ptr),				\
	  "i" (-EFAULT));						\
}

extern void __put_user_unknown(void);

extern size_t __copy_user(void *to, const void *from, size_t n,
			  const void *from_end);
extern size_t __copy_from_user_eva(void *to, const void *from, size_t n,
				   const void *from_end);
extern size_t __copy_to_user_eva(void *to, const void *from, size_t n,
				 const void *from_end);
extern size_t __copy_in_user_eva(void *to, const void *from, size_t n,
				 const void *from_end);

static inline unsigned long
raw_copy_to_user(void __user *to, const void *from, unsigned long n)
{
	if (eva_user_access())
		return __copy_to_user_eva(to, from, n, from + n);
	else
		return __copy_user(to, from, n, from + n);
}

static inline unsigned long
raw_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	if (eva_user_access())
		return __copy_from_user_eva(to, from, n, from + n);
	else
		return __copy_user(to, from, n, from + n);
}

#define INLINE_COPY_FROM_USER
#define INLINE_COPY_TO_USER

static inline unsigned long
raw_copy_in_user(void __user*to, const void __user *from, unsigned long n)
{
	if (eva_user_access())
		return __copy_in_user_eva(to, from, n, from + n);
	else
		return __copy_user(to, from, n, from + n);
}

extern __kernel_size_t __bzero_kernel(void __user *addr, int val, __kernel_size_t size);
extern __kernel_size_t __bzero(void __user *addr, int val, __kernel_size_t size);

/*
 * __clear_user: - Zero a block of memory in user space, with less checking.
 * @to:	  Destination address, in user space.
 * @n:	  Number of bytes to zero.
 *
 * Zero a block of memory in user space.  Caller must check
 * the specified block with access_ok() before calling this function.
 *
 * Returns number of bytes that could not be cleared.
 * On success, this will be zero.
 */
static inline __kernel_size_t
__clear_user(void __user *addr, __kernel_size_t size)
{
	if (eva_kernel_access())
		return __bzero_kernel(addr, 0, size);

	might_fault();
	return __bzero(addr, 0, size);
}

#define clear_user(addr,n)						\
({									\
	void __user * __cl_addr = (addr);				\
	unsigned long __cl_size = (n);					\
	if (__cl_size && access_ok(VERIFY_WRITE,			\
					__cl_addr, __cl_size))		\
		__cl_size = __clear_user(__cl_addr, __cl_size);		\
	__cl_size;							\
})

extern long __strncpy_from_kernel_asm(char *__to, const char __user *__from, long __len);
extern long __strncpy_from_user_asm(char *__to, const char __user *__from, long __len);

/*
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *	   least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
static inline long
strncpy_from_user(char *__to, const char __user *__from, long __len)
{
	if (eva_kernel_access())
		return __strncpy_from_kernel_asm(__to, __from, __len);

	might_fault();
	return __strncpy_from_user_asm(__to, __from, __len);
}

extern long __strnlen_kernel_asm(const char __user *s, long n);
extern long __strnlen_user_asm(const char __user *s, long n);

/*
 * strnlen_user: - Get the size of a string in user space.
 * @str: The string to measure.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 * If the string is too long, returns a value greater than @n.
 */
static inline long strnlen_user(const char __user *s, long n)
{
	might_fault();

	if (eva_kernel_access())
		return __strnlen_kernel_asm(s, n);

	return __strnlen_user_asm(s, n);
}

#endif /* _ASM_UACCESS_H */
