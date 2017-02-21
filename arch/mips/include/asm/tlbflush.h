#ifndef __ASM_TLBFLUSH_H
#define __ASM_TLBFLUSH_H

#include <linux/mm.h>
#include <asm/mipsregs.h>

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLB entries
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB entries
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 */
extern void local_flush_tlb_all(void);
extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end);
extern void local_flush_tlb_kernel_range(unsigned long start,
	unsigned long end);
extern void local_flush_tlb_page(struct vm_area_struct *vma,
	unsigned long page);
extern void local_flush_tlb_one(unsigned long vaddr);

#ifdef CONFIG_SMP

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long,
	unsigned long);
extern void flush_tlb_kernel_range(unsigned long, unsigned long);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);
extern void flush_tlb_one(unsigned long vaddr);

#else /* CONFIG_SMP */

#define flush_tlb_all()			local_flush_tlb_all()
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_range(vma, vmaddr, end)	local_flush_tlb_range(vma, vmaddr, end)
#define flush_tlb_kernel_range(vmaddr,end) \
	local_flush_tlb_kernel_range(vmaddr, end)
#define flush_tlb_page(vma, page)	local_flush_tlb_page(vma, page)
#define flush_tlb_one(vaddr)		local_flush_tlb_one(vaddr)

#endif /* CONFIG_SMP */

enum mips_global_tlb_invalidate_type {
	invalidate_all_tlb,
	invalidate_by_va,
	invalidate_by_mmid,
	invalidate_by_va_mmid,
};

#ifdef TOOLCHAIN_SUPPORTS_GINV

#define ginvt(page, type)					\
do {								\
	__asm__ __volatile__(					\
		".set	push\n\t"				\
		".set	ginv\n\t"				\
		"ginvt	%0, %1\n\t"				\
		".set	pop\n\t"				\
	: /* No outputs */					\
	: "r" (page), "i" (type)				\
	);							\
} while(0)

#else	/* TOOLCHAIN_SUPPORTS_GINV */

#define ginvt(page, type)					\
do {								\
	__asm__ __volatile__(					\
		".set	push\n\t"				\
		".set	noat\n\t"				\
		"move	$1, %0\n\t"				\
		"# ginvt $1, %1\n\t"				\
		_ASM_INSN_IF_MIPS(0x7c2000bd | (%1 << 8))	\
		_ASM_INSN32_IF_MM(0x0001717c | (%1 << 9))	\
		".set	pop\n\t"				\
	: /* No outputs */					\
	: "r" (page), "i" (type)				\
	);							\
} while(0)

#endif	/* !TOOLCHAIN_SUPPORTS_GINV */

static inline void global_tlb_invalidate(
	unsigned long page, unsigned long type)
{
	switch (type) {
	case invalidate_all_tlb:
		ginvt(0, invalidate_all_tlb);
		break;
	case invalidate_by_va:
		page &= (PAGE_MASK << 1);
		ginvt(page, invalidate_by_va);
		break;
	case invalidate_by_mmid:
		ginvt(0, invalidate_by_mmid);
		break;
	case invalidate_by_va_mmid:
		page &= (PAGE_MASK << 1);
		ginvt(page, invalidate_by_va_mmid);
		break;
	}
	sync_ginv();
}

#endif /* __ASM_TLBFLUSH_H */
