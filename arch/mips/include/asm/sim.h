/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_SIM_H
#define _ASM_SIM_H


#include <asm/asm-offsets.h>

#define __str2(x) #x
#define __str(x) __str2(x)

#ifdef CONFIG_32BIT

#define save_static_function(symbol)					\
__asm__(								\
	".text\n\t"							\
	".globl\t__" #symbol "\n\t"					\
	".align\t2\n\t"							\
	".type\t__" #symbol ", @function\n\t"				\
	".ent\t__" #symbol ", 0\n__"					\
	#symbol":\n\t"							\
	".frame\t$sp, 0, $ra\n\t"					\
	"sw\t$s0,"__str(PT_R16)"($sp)\t\t\t# save_static_function\n\t"	\
	"sw\t$s1,"__str(PT_R17)"($sp)\n\t"				\
	"sw\t$s2,"__str(PT_R18)"($sp)\n\t"				\
	"sw\t$s3,"__str(PT_R19)"($sp)\n\t"				\
	"sw\t$s4,"__str(PT_R20)"($sp)\n\t"				\
	"sw\t$s5,"__str(PT_R21)"($sp)\n\t"				\
	"sw\t$s6,"__str(PT_R22)"($sp)\n\t"				\
	"sw\t$s7,"__str(PT_R23)"($sp)\n\t"				\
	"sw\t$fp,"__str(PT_R30)"($sp)\n\t"				\
	"j\t" #symbol "\n\t"						\
	".end\t__" #symbol "\n\t"					\
	".size\t__" #symbol",. - __" #symbol)

#endif /* CONFIG_32BIT */

#ifdef CONFIG_64BIT

#define save_static_function(symbol)					\
__asm__(								\
	".text\n\t"							\
	".globl\t__" #symbol "\n\t"					\
	".align\t2\n\t"							\
	".type\t__" #symbol ", @function\n\t"				\
	".ent\t__" #symbol ", 0\n__"					\
	#symbol":\n\t"							\
	".frame\t$sp, 0, $ra\n\t"					\
	"sd\t$s0,"__str(PT_R16)"($sp)\t\t\t# save_static_function\n\t"	\
	"sd\t$s1,"__str(PT_R17)"($sp)\n\t"				\
	"sd\t$s2,"__str(PT_R18)"($sp)\n\t"				\
	"sd\t$s3,"__str(PT_R19)"($sp)\n\t"				\
	"sd\t$s4,"__str(PT_R20)"($sp)\n\t"				\
	"sd\t$s5,"__str(PT_R21)"($sp)\n\t"				\
	"sd\t$s6,"__str(PT_R22)"($sp)\n\t"				\
	"sd\t$s7,"__str(PT_R23)"($sp)\n\t"				\
	"sd\t$fp,"__str(PT_R30)"($sp)\n\t"				\
	"j\t" #symbol "\n\t"						\
	".end\t__" #symbol "\n\t"					\
	".size\t__" #symbol",. - __" #symbol)

#endif /* CONFIG_64BIT */

#endif /* _ASM_SIM_H */
