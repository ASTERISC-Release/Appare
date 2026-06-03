// SPDX-License-Identifier: GPL-2.0-only
/*
 * common.c - C code for kernel entry and exit
 * Copyright (c) 2015 Andrew Lutomirski
 *
 * Based on asm and ptrace code by many authors.  The code here originated
 * in ptrace.c and signal.c.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/entry-common.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/export.h>
#include <linux/nospec.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#ifdef CONFIG_XEN_PV
#include <xen/xen-ops.h>
#include <xen/events.h>
#endif

#include <asm/desc.h>
#include <asm/traps.h>
#include <asm/vdso.h>
#include <asm/cpufeature.h>
#include <asm/fpu/api.h>
#include <asm/nospec-branch.h>
#include <asm/io_bitmap.h>
#include <asm/syscall.h>
#include <asm/irq_stack.h>

#ifdef CONFIG_X86_64

// deeplog
int DEEPLOG_IS_CF_LOGGING;
EXPORT_SYMBOL(DEEPLOG_IS_CF_LOGGING);

// #define ENABLE_OMNILOG
int DEEPLOG_IS_PT_TRACING;
EXPORT_SYMBOL(DEEPLOG_IS_PT_TRACING);


struct omnisys_log_t { // this is around 8 * 8 + 128 = 192 bytes
	unsigned long nr;
	unsigned long rdi;
	unsigned long rsi;
	unsigned long rdx;
	unsigned long r10;
	unsigned long r8;
	unsigned long r9;
	unsigned long tsc;

	char padding[128];
};

static __always_inline bool is_tracked_proc(void)
{
  /* CHUQI: I went ahead and added all the omnilog ones, I don't believe these are the right names. I will correct as I build profiles
   * it should not affect the base addresses though (fingers crossed) will tell you if it does */
  char audited_processes[9][15] = {"nginx", "memcached", "redis-server", "sqlite", "chromium", "openssl", "7zip", "gnu-octave", "geomean"}; 

  for (int i = 0; i < 9; i++) {
    if (strstr(current->comm, audited_processes[i]) != NULL) {
       return true;
    }
  }
  return false;
}

static __always_inline void do_omnilog_syslog(struct pt_regs *regs, int nr)
{
	/* Chuqi: stupid impl of OmniLog */
	struct omnisys_log_t log;
	unsigned int hypercall_nr = 0x40004;
	unsigned long src_gva = (unsigned long)&log;
	unsigned long log_length = sizeof(struct omnisys_log_t);
	unsigned long syscall_id = nr;

	long ret;
	// rdtsc
	unsigned int lo, hi;
    asm volatile(
        "rdtsc"
        : "=a"(lo), "=d"(hi)
        :
        : "memory"
    );

	log.nr = nr;
	log.rdi = regs->di;
	log.rsi = regs->si;
	log.rdx = regs->dx;
	log.r10 = regs->r10;
	log.r8 = regs->r8;
	log.r9 = regs->r9;
	log.tsc = (((unsigned long)hi << 32) | lo);
	
	// hypercall
	asm volatile(
		"vmcall"
		: "=a"(ret)
		: "a"(hypercall_nr), "b"(src_gva), "c"(log_length), "d"(syscall_id)
		: "memory"
	);
	// printk("Current process=%s, syscall=%d, asked vmcall_id=%d\n", current->comm, nr, hypercall_nr);
	return;
}


static __always_inline void do_start_stop_pt(int start, int nr)
{
	/* Chuqi: stupid impl of always start/stop PT */
	unsigned int hypercall_nr;
	long ret;
	if (start) {
		hypercall_nr = 0x20011;
	} else {
		hypercall_nr = 0x82350;
	}

	// if (!DEEPLOG_IS_PT_TRACING) {
	// 	return;
	// }

	// hypercall
	asm volatile(
		"vmcall"
		: "=a"(ret)
		: "a"(hypercall_nr), "b"(nr), "c"(0), "d"(0)
		: "memory"
	);
	return;
}

static __always_inline bool do_syscall_x64(struct pt_regs *regs, int nr)
{
	/*
	 * Convert negative numbers to very high and thus out of range
	 * numbers for comparisons.
	 */
	unsigned int unr = nr;
	// for deeplog
	register unsigned int r1 asm("ecx") = 0;
	// register unsigned long edi asm("rbx") = (unsigned long)unr;
	register unsigned int eax asm("eax") = unr;

#ifdef ENABLE_OMNILOG
	if (likely(unr < NR_syscalls)) {
		if (is_tracked_proc()) {
			do_omnilog_syslog(regs, unr);
		}
	}
#endif

	// normal deeplog vmfunc function
	if (likely(unr < NR_syscalls) && DEEPLOG_IS_CF_LOGGING == 1) {
		if (is_tracked_proc()) {
			asm("xor %eax, %eax");
			asm("movl %0,%1;"
			:
			: "r" (unr), "r" (r1));
			// asm("add $0x1, %ecx");
			asm("vmfunc");
			asm("nop");
			asm("nop");
			asm("nop");
		}
	}

	if (likely(unr < NR_syscalls)) {
		// profile: always PT trace all (start)
#if (0) // disabled
		if (is_tracked_proc()) {
			do_start_stop_pt(1, unr);
		}
#endif

		unr = array_index_nospec(unr, NR_syscalls);
		regs->ax = sys_call_table[unr](regs);

		// normal deeplog vmfunc function
		// vmcall + vmfunc (back)
		if (DEEPLOG_IS_CF_LOGGING == 1) {
			if (is_tracked_proc()) {
			  asm("movl %0,%1;"
				:
				: "r" (0x82350), "r" (eax));
			  asm volatile ("vmcall");
			  asm("nop");
			  asm("nop");
			  asm("nop");
			  asm("movl %0,%1;"
				:
				: "r" (511), "r" (r1));
			  asm("xor %rax, %rax");
			  asm("vmfunc");
			}
		  }

		// profile: always PT trace all (stop)
#if (0) // disabled
		if (is_tracked_proc()) {
			do_start_stop_pt(0, unr);
		}
#endif
		return true;
	}
	return false;
}

static __always_inline bool do_syscall_x32(struct pt_regs *regs, int nr)
{
	/*
	 * Adjust the starting offset of the table, and convert numbers
	 * < __X32_SYSCALL_BIT to very high and thus out of range
	 * numbers for comparisons.
	 */
	unsigned int xnr = nr - __X32_SYSCALL_BIT;

	if (IS_ENABLED(CONFIG_X86_X32_ABI) && likely(xnr < X32_NR_syscalls)) {
		xnr = array_index_nospec(xnr, X32_NR_syscalls);
		regs->ax = x32_sys_call_table[xnr](regs);
		return true;
	}
	return false;
}

__visible noinstr void do_syscall_64(struct pt_regs *regs, int nr)
{
	add_random_kstack_offset();
	nr = syscall_enter_from_user_mode(regs, nr);

	instrumentation_begin();

	if (!do_syscall_x64(regs, nr) && !do_syscall_x32(regs, nr) && nr != -1) {
		/* Invalid system call, but still a system call. */
		regs->ax = __x64_sys_ni_syscall(regs);
	}

	instrumentation_end();
	syscall_exit_to_user_mode(regs);
}
#endif

#if defined(CONFIG_X86_32) || defined(CONFIG_IA32_EMULATION)
static __always_inline int syscall_32_enter(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_IA32_EMULATION))
		current_thread_info()->status |= TS_COMPAT;

	return (int)regs->orig_ax;
}

/*
 * Invoke a 32-bit syscall.  Called with IRQs on in CONTEXT_KERNEL.
 */
static __always_inline void do_syscall_32_irqs_on(struct pt_regs *regs, int nr)
{
	/*
	 * Convert negative numbers to very high and thus out of range
	 * numbers for comparisons.
	 */
	unsigned int unr = nr;

	if (likely(unr < IA32_NR_syscalls)) {
		unr = array_index_nospec(unr, IA32_NR_syscalls);
		regs->ax = ia32_sys_call_table[unr](regs);
	} else if (nr != -1) {
		regs->ax = __ia32_sys_ni_syscall(regs);
	}
}

/* Handles int $0x80 */
__visible noinstr void do_int80_syscall_32(struct pt_regs *regs)
{
	int nr = syscall_32_enter(regs);

	add_random_kstack_offset();
	/*
	 * Subtlety here: if ptrace pokes something larger than 2^31-1 into
	 * orig_ax, the int return value truncates it. This matches
	 * the semantics of syscall_get_nr().
	 */
	nr = syscall_enter_from_user_mode(regs, nr);
	instrumentation_begin();

	do_syscall_32_irqs_on(regs, nr);

	instrumentation_end();
	syscall_exit_to_user_mode(regs);
}

static noinstr bool __do_fast_syscall_32(struct pt_regs *regs)
{
	int nr = syscall_32_enter(regs);
	int res;

	add_random_kstack_offset();
	/*
	 * This cannot use syscall_enter_from_user_mode() as it has to
	 * fetch EBP before invoking any of the syscall entry work
	 * functions.
	 */
	syscall_enter_from_user_mode_prepare(regs);

	instrumentation_begin();
	/* Fetch EBP from where the vDSO stashed it. */
	if (IS_ENABLED(CONFIG_X86_64)) {
		/*
		 * Micro-optimization: the pointer we're following is
		 * explicitly 32 bits, so it can't be out of range.
		 */
		res = __get_user(*(u32 *)&regs->bp,
			 (u32 __user __force *)(unsigned long)(u32)regs->sp);
	} else {
		res = get_user(*(u32 *)&regs->bp,
		       (u32 __user __force *)(unsigned long)(u32)regs->sp);
	}

	if (res) {
		/* User code screwed up. */
		regs->ax = -EFAULT;

		local_irq_disable();
		instrumentation_end();
		irqentry_exit_to_user_mode(regs);
		return false;
	}

	nr = syscall_enter_from_user_mode_work(regs, nr);

	/* Now this is just like a normal syscall. */
	do_syscall_32_irqs_on(regs, nr);

	instrumentation_end();
	syscall_exit_to_user_mode(regs);
	return true;
}

/* Returns 0 to return using IRET or 1 to return using SYSEXIT/SYSRETL. */
__visible noinstr long do_fast_syscall_32(struct pt_regs *regs)
{
	/*
	 * Called using the internal vDSO SYSENTER/SYSCALL32 calling
	 * convention.  Adjust regs so it looks like we entered using int80.
	 */
	unsigned long landing_pad = (unsigned long)current->mm->context.vdso +
					vdso_image_32.sym_int80_landing_pad;

	/*
	 * SYSENTER loses EIP, and even SYSCALL32 needs us to skip forward
	 * so that 'regs->ip -= 2' lands back on an int $0x80 instruction.
	 * Fix it up.
	 */
	regs->ip = landing_pad;

	/* Invoke the syscall. If it failed, keep it simple: use IRET. */
	if (!__do_fast_syscall_32(regs))
		return 0;

#ifdef CONFIG_X86_64
	/*
	 * Opportunistic SYSRETL: if possible, try to return using SYSRETL.
	 * SYSRETL is available on all 64-bit CPUs, so we don't need to
	 * bother with SYSEXIT.
	 *
	 * Unlike 64-bit opportunistic SYSRET, we can't check that CX == IP,
	 * because the ECX fixup above will ensure that this is essentially
	 * never the case.
	 */
	return regs->cs == __USER32_CS && regs->ss == __USER_DS &&
		regs->ip == landing_pad &&
		(regs->flags & (X86_EFLAGS_RF | X86_EFLAGS_TF)) == 0;
#else
	/*
	 * Opportunistic SYSEXIT: if possible, try to return using SYSEXIT.
	 *
	 * Unlike 64-bit opportunistic SYSRET, we can't check that CX == IP,
	 * because the ECX fixup above will ensure that this is essentially
	 * never the case.
	 *
	 * We don't allow syscalls at all from VM86 mode, but we still
	 * need to check VM, because we might be returning from sys_vm86.
	 */
	return static_cpu_has(X86_FEATURE_SEP) &&
		regs->cs == __USER_CS && regs->ss == __USER_DS &&
		regs->ip == landing_pad &&
		(regs->flags & (X86_EFLAGS_RF | X86_EFLAGS_TF | X86_EFLAGS_VM)) == 0;
#endif
}

/* Returns 0 to return using IRET or 1 to return using SYSEXIT/SYSRETL. */
__visible noinstr long do_SYSENTER_32(struct pt_regs *regs)
{
	/* SYSENTER loses RSP, but the vDSO saved it in RBP. */
	regs->sp = regs->bp;

	/* SYSENTER clobbers EFLAGS.IF.  Assume it was set in usermode. */
	regs->flags |= X86_EFLAGS_IF;

	return do_fast_syscall_32(regs);
}
#endif

SYSCALL_DEFINE0(ni_syscall)
{
	return -ENOSYS;
}

#ifdef CONFIG_XEN_PV
#ifndef CONFIG_PREEMPTION
/*
 * Some hypercalls issued by the toolstack can take many 10s of
 * seconds. Allow tasks running hypercalls via the privcmd driver to
 * be voluntarily preempted even if full kernel preemption is
 * disabled.
 *
 * Such preemptible hypercalls are bracketed by
 * xen_preemptible_hcall_begin() and xen_preemptible_hcall_end()
 * calls.
 */
DEFINE_PER_CPU(bool, xen_in_preemptible_hcall);
EXPORT_SYMBOL_GPL(xen_in_preemptible_hcall);

/*
 * In case of scheduling the flag must be cleared and restored after
 * returning from schedule as the task might move to a different CPU.
 */
static __always_inline bool get_and_clear_inhcall(void)
{
	bool inhcall = __this_cpu_read(xen_in_preemptible_hcall);

	__this_cpu_write(xen_in_preemptible_hcall, false);
	return inhcall;
}

static __always_inline void restore_inhcall(bool inhcall)
{
	__this_cpu_write(xen_in_preemptible_hcall, inhcall);
}
#else
static __always_inline bool get_and_clear_inhcall(void) { return false; }
static __always_inline void restore_inhcall(bool inhcall) { }
#endif

static void __xen_pv_evtchn_do_upcall(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	inc_irq_stat(irq_hv_callback_count);

	xen_hvm_evtchn_do_upcall();

	set_irq_regs(old_regs);
}

__visible noinstr void xen_pv_evtchn_do_upcall(struct pt_regs *regs)
{
	irqentry_state_t state = irqentry_enter(regs);
	bool inhcall;

	instrumentation_begin();
	run_sysvec_on_irqstack_cond(__xen_pv_evtchn_do_upcall, regs);

	inhcall = get_and_clear_inhcall();
	if (inhcall && !WARN_ON_ONCE(state.exit_rcu)) {
		irqentry_exit_cond_resched();
		instrumentation_end();
		restore_inhcall(inhcall);
	} else {
		instrumentation_end();
		irqentry_exit(regs, state);
	}
}
#endif /* CONFIG_XEN_PV */
