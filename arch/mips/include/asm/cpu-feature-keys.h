/* SPDX-License-Identifier: GPL-2.0 */

CPU_KEY(32fpr)			/* 32 dbl. prec. FP registers */
CPU_KEY(3k_cache)		/* R3000-style caches */
CPU_KEY(4k_cache)		/* R4000-style caches */
CPU_KEY(4kex)			/* "R4K" exception model */
CPU_KEY(badinstr)		/* CPU has BadInstr register */
CPU_KEY(badinstrp)		/* CPU has BadInstrP register */
CPU_KEY(bp_ghist)		/* R12K+ Branch Prediction Global History */
CPU_KEY(cache_cdex_p)		/* Create_Dirty_Exclusive CACHE op */
CPU_KEY(cache_cdex_s)		/* ... same for seconary cache ... */
CPU_KEY(cdmm)			/* CPU has Common Device Memory Map */
CPU_KEY(contextconfig)		/* CPU has [X]ConfigContext registers */
CPU_KEY(counter)		/* Cycle count/compare */
CPU_KEY(divec)			/* dedicated interrupt vector */
CPU_KEY(drg)			/* CPU has VZ Direct Root to Guest (DRG) */
CPU_KEY(ebase_wg)		/* CPU has EBase.WG */
CPU_KEY(ejtag)			/* EJTAG exception */
CPU_KEY(eva)			/* CPU supports Enhanced Virtual Addressing */
CPU_KEY(fre)			/* FRE & UFE bits implemented */
CPU_KEY(ftlb)			/* CPU has Fixed-page-size TLB */
CPU_KEY(guestctl0ext)		/* CPU has VZ GuestCtl0Ext register */
CPU_KEY(guestctl1)		/* CPU has VZ GuestCtl1 register */
CPU_KEY(guestctl2)		/* CPU has VZ GuestCtl2 register */
CPU_KEY(guestid)		/* CPU uses VZ ASE GuestID feature */
CPU_KEY(htw)			/* CPU support Hardware Page Table Walker */
CPU_KEY(inclusive_pcaches)	/* P-cache subset enforced */
CPU_KEY(ldpte)			/* CPU has ldpte/lddir instructions */
CPU_KEY(llsc)			/* CPU has ll/sc instructions */
CPU_KEY(lpa)			/* CPU supports Large Physical Addressing */
CPU_KEY(maar)			/* MAAR(I) registers are present */
CPU_KEY(mcheck)			/* Machine check exception */
CPU_KEY(mipsmt_pertccounters)	/* CPU has perf counters implemented per TC (MIPSMT ASE) */
CPU_KEY(mmips)			/* CPU has microMIPS capability */
CPU_KEY(mmid)			/* CPU supports MemoryMapIDs */
CPU_KEY(mvh)			/* CPU supports MFHC0/MTHC0 */
CPU_KEY(nan_2008)		/* 2008 NaN implemented */
CPU_KEY(nan_legacy)		/* Legacy NaN implemented */
CPU_KEY(nofpuex)		/* no FPU exception */
CPU_KEY(perf)			/* CPU has MIPS performance counters */
CPU_KEY(perf_cntr_intr_bit)	/* CPU has Perf Ctr Int indicator */
CPU_KEY(prefetch)		/* CPU has usable prefetch */
CPU_KEY(rixi)			/* CPU has TLB Read/eXec Inhibit */
CPU_KEY(rixiex)			/* CPU has unique exception codes for {Read, Execute}-Inhibit exceptions */
CPU_KEY(rw_llb)			/* LLADDR/LLB writes are allowed */
CPU_KEY(segments)		/* CPU supports Segmentation Control registers */
CPU_KEY(shared_ftlb_entries)	/* CPU shares FTLB entries with another */
CPU_KEY(shared_ftlb_ram)	/* CPU shares FTLB RAM with another */
CPU_KEY(small_pages)		/* Small (1KB) page support */
CPU_KEY(tlb)			/* CPU has TLB */
CPU_KEY(tlbinv)			/* CPU supports TLBINV/F */
CPU_KEY(tx39_cache)		/* TX3900-style caches */
CPU_KEY(ufr)			/* CPU supports User mode FR switching */
CPU_KEY(userlocal)		/* CPU has ULRI feature */
CPU_KEY(vce)			/* virt. coherence conflict possible */
CPU_KEY(veic)			/* CPU supports MIPSR2 external interrupt controller mode */
CPU_KEY(vint)			/* CPU supports MIPSR2 vectored interrupts */
CPU_KEY(vp)			/* MIPSr6 Virtual Processors (multi-threading) */

CPU_GUEST_KEY(badinstr)
CPU_GUEST_KEY(badinstrp)
CPU_GUEST_KEY(contextconfig)
CPU_GUEST_KEY(dyn_maar)
CPU_GUEST_KEY(htw)
CPU_GUEST_KEY(maar)
CPU_GUEST_KEY(mvh)
CPU_GUEST_KEY(rw_llb)
CPU_GUEST_KEY(segments)
CPU_GUEST_KEY(userlocal)
