#ifndef __KVM_X86_VMX_LIBPT_H
#define __KVM_X86_VMX_LIBPT_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/cma.h>
#include <linux/dma-map-ops.h>

// #include <linux/mmu_context.h>
#include <linux/mman.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include <asm/msr-index.h>
#include <linux/sched.h>
#include <asm/msr.h>
#include <asm/cpufeature.h>

#include "common.h"
#include "lbr.h"

// #include "metadata.h"
// #define pt_avail() test_cpu_cap(&boot_cpu_data, X86_FEATURE_INTEL_PT)
// #define pt_enabled() (native_read_msr(MSR_IA32_RTIT_CTL) & RTIT_CTL_TRACEEN)
// #define PT_LOG_FPATH(i)			"/var/log/pt.log." #i

#define TOPA_ENTRY_SIZE_4K      0
#define TOPA_ENTRY_SIZE_8K      1
#define TOPA_ENTRY_SIZE_16K     2
#define TOPA_ENTRY_SIZE_32K     3
#define TOPA_ENTRY_SIZE_64K     4
#define TOPA_ENTRY_SIZE_128K    5
#define TOPA_ENTRY_SIZE_256K    6
#define TOPA_ENTRY_SIZE_512K    7
#define TOPA_ENTRY_SIZE_1M      8
#define TOPA_ENTRY_SIZE_2M      9
#define TOPA_ENTRY_SIZE_4M      10
#define TOPA_ENTRY_SIZE_8M      11
#define TOPA_ENTRY_SIZE_16M     12
#define TOPA_ENTRY_SIZE_32M     13
#define TOPA_ENTRY_SIZE_64M     14
#define TOPA_ENTRY_SIZE_128M    15

// #define TOPA_ENTRY_SIZE_CHOICE  TOPA_ENTRY_SIZE_64M     // all logging
#define TOPA_ENTRY_SIZE_CHOICE  TOPA_ENTRY_SIZE_8M
#define TOPA_BUFFER_SIZE        (1 << (12 + TOPA_ENTRY_SIZE_CHOICE))

#define PSBFREQ_SIZE_CHOICE		TOPA_ENTRY_SIZE_64K

#define SIZE_64B 64
#define SIZE_4K PAGE_SIZE
#define SIZE_64M PAGE_SIZE * 16384
#define SIZE_128M PAGE_SIZE * 32768
#define SIZE_1024M PAGE_SIZE * 262144
#define MEMORY_SIZE SIZE_64B

struct topa_entry {
	u64 end:1;
	u64 rsvd0:1;
	u64 intr:1;
	u64 rsvd1:1;
	u64 stop:1;
	u64 rsvd2:1;
	u64 size:4;
	u64 rsvd3:2;
	u64 base:36;
	u64 rsvd4:16;
};

#define TOPA_ENTRY(_base, _size, _stop, _intr, _end) (struct topa_entry) { \
	.base = (_base) >> 12, \
	.size = (_size), \
	.stop = (_stop), \
	.intr = (_intr), \
	.end = (_end), \
}

struct topa {
	struct topa_entry entries[3];
	char *raw;
	// struct task_struct *task;
	u64 sequence;
	u64 n_processed;
	struct list_head buffer_list;
	spinlock_t buffer_list_sl;
	bool failed;
	int index;
};

struct pt_buffer {
	// worker to transfer pt traces from memory to file
	struct work_struct work;
	// corresponding vcpu
	struct kvm_vcpu *vcpu;
	// tasklet is used to queue up works to be done at a later time.
	struct tasklet_struct tasklet;
	struct list_head entry;
	struct topa *topa;
	struct topa *child_topa;
	u64 sequence;
	char *raw;
	u32 size;
	int index;
};


int vmx_init_pt(void);
void vmx_exit_pt(void);

void pt_on_interrupt(struct kvm_vcpu *vcpu);

/* initialize PT buffer */
void deeplog_pt_init(struct kvm_vcpu *vcpu);

/* start PT */
void deeplog_pt_start(struct kvm_vcpu *vcpu);

void deeplog_pt_init_start(struct kvm_vcpu *vcpu, int syscall_id);

/* stop PT */
void deeplog_pt_stop(struct kvm_vcpu *vcpu);

/* stop PT */
void deeplog_pt_destroy(struct kvm_vcpu *vcpu);

/* debug: vmem info */
void deeplog_pt_vmeminfo(struct kvm_vcpu *vcpu);

/* PT memory dump */
void deeplog_pt_memdump(struct kvm_vcpu *vcpu);

void vmcall_sysend_pt_stop(struct kvm_vcpu *vcpu);

/* micro-benchs */
void micro_vmcall_PT_config(struct kvm_vcpu *vcpu);

unsigned long deeplog_pt_get_rebase(void);
#endif