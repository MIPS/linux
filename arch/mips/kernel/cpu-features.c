/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/asm-offsets.h>
#include <asm/cpu-features.h>

#define __CPU_KEY_0(pfx, name)						\
	void __set_cpu##pfx##has_##name(bool enabled)			\
	{								\
	}

#define __CPU_KEY_1(pfx, name)						\
	DEFINE_STATIC_KEY_FALSE(__cpu##pfx##has_##name);		\
									\
	void __set_cpu##pfx##has_##name(bool enabled)			\
	{								\
		if (enabled)						\
			static_branch_enable(&__cpu##pfx##has_##name);	\
		else							\
			static_branch_disable(&__cpu##pfx##has_##name);	\
	}

#define __CPU_KEY(pfx, name, need)	__CPU_KEY_##need(pfx, name)
#define _CPU_KEY(pfx, name, need)	__CPU_KEY(pfx, name, need)

#define CPU_KEY(name)			_CPU_KEY(_, name, __cpu_has_key_##name)
#define CPU_GUEST_KEY(name)		_CPU_KEY(_guest_, name, __cpu_guest_has_key_##name)

#include <asm/cpu-feature-keys.h>
