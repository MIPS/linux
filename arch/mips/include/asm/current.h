#ifndef __ASM_CURRENT_H
#define __ASM_CURRENT_H

#include <linux/compiler.h>

#ifndef __ASSEMBLY__

struct task_struct;

/* How to get the thread information struct from C.  */
register struct task_struct *__current_task __asm__("$28");

static __always_inline struct task_struct *get_current(void)
{
	return __current_task;
}

#define current get_current()

#endif /* __ASSEMBLY__ */

#endif /* __ASM_CURRENT_H */
