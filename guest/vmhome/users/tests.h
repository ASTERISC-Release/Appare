#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define SZ_4KB      (1 << 12)

#define KVM_DEEPLOG_INIT_DEEPLOG_CONTEXTS	0x19999
#define KVM_DEEPLOG_ACT_DEEPLOG_CONTEXTS	0x20000
#define KVM_DEEPLOG_ACT_DEFAULT_CONTEXTS    0x20001
#define KVM_DEEPLOG_OBJ_PROFILING    0x2000c
#define KVM_DEEPLOG_PIN_MEMORY    0x40002
#define KVM_DEEPLOG_WALK_MEMORY    0x40003
#define KVM_DEEPLOG_WALK_MEMORY    0x40003
#define KVM_DEEPLOG_DUMPLBR         0x40005

typedef unsigned long long  u64;

static inline long kvm_hypercall3(unsigned int nr, unsigned long p1,
				  unsigned long p2, unsigned long p3)
{
	long ret;
	asm volatile(
             "vmcall"
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3)
		     : "memory");
	return ret;
}

static inline void vmfunc(u64 input_rax, u64 input_rcx) {
    asm volatile(
        "vmfunc"
        :
        : "a"(input_rax), "c"(input_rcx)
        : "memory"
    );
}

static inline uint64_t rdtsc() {
    unsigned int lo, hi;
    asm volatile(
        "rdtsc"
        : "=a"(lo), "=d"(hi)
        :
        : "memory"
    );
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtscp() {
    unsigned int lo, hi;
    asm volatile(
        "rdtscp"
        : "=a"(lo), "=d"(hi)
        :
        : "memory"
    );
    return ((uint64_t)hi << 32) | lo;
}

static inline void PT_init(void) {
    kvm_hypercall3(0x20005, 0, 0, 0);
}

static inline void PT_enable(void) {
    kvm_hypercall3(0x20005, 0, 0, 0);
}

static inline void PT_disable(void) {
    kvm_hypercall3(0x20008, 0, 0, 0);
}

static inline void dmesg_log_statistics(void) {
    kvm_hypercall3(0x82348, 0, 0, 0);
}

static inline void Pin_guest_memory(void)
{
    kvm_hypercall3(KVM_DEEPLOG_PIN_MEMORY, 0, 0, 0);
}

static inline void Walk_guest_memory_done(void)
{
    kvm_hypercall3(KVM_DEEPLOG_WALK_MEMORY, 0, 0, 0);
}

static inline void Dump_guest_lbr(void)
{
    kvm_hypercall3(KVM_DEEPLOG_DUMPLBR, 0, 0, 0);
}
