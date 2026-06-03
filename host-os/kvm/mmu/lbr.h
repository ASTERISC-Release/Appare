/* This is included in `libpt.h` */

/* Architectural LBR MSR definitions */
// #define MSR_ARCH_LBR_CTL        0x000014ce
// #define MSR_ARCH_LBR_DEPTH      0x000014cf
// #define MSR_ARCH_LBR_FROM_0     0x00001500
// #define MSR_ARCH_LBR_TO_0       0x00001600  
// #define MSR_ARCH_LBR_INFO_0     0x00001200

/* LBR Control bits */
#define ARCH_LBR_CTL_ENABLE        (1ULL << 0)
#define ARCH_LBR_CTL_KERNEL        (1ULL << 1)
#define ARCH_LBR_CTL_USER          (1ULL << 2)
#define ARCH_LBR_CTL_CALL_STACK    (1ULL << 3)

#define ARCH_LBR_CTL_JCC           (1ULL << 16)
#define ARCH_LBR_CTL_REL_JMP       (1ULL << 17)
#define ARCH_LBR_CTL_IND_JMP       (1ULL << 18)
#define ARCH_LBR_CTL_REL_CALL      (1ULL << 19)
#define ARCH_LBR_CTL_IND_CALL      (1ULL << 20)
#define ARCH_LBR_CTL_RETURN        (1ULL << 21)
#define ARCH_LBR_CTL_OTHER_BRANCH  (1ULL << 22)

#define ARCH_LBR_ALL_BRANCHES  (ARCH_LBR_CTL_JCC | ARCH_LBR_CTL_REL_JMP | \
                                ARCH_LBR_CTL_IND_JMP | ARCH_LBR_CTL_REL_CALL | \
                                ARCH_LBR_CTL_IND_CALL | ARCH_LBR_CTL_RETURN | \
                                ARCH_LBR_CTL_OTHER_BRANCH)

#define ARCH_LBR_CALLSTACK_CALL_BRANCHES (ARCH_LBR_CTL_REL_CALL | ARCH_LBR_CTL_IND_CALL | \
                                        ARCH_LBR_CTL_RETURN | ARCH_LBR_CTL_CALL_STACK)

/* Max LBR depth (32 entries) */
#define MAX_ARCH_LBR_DEPTH       32

/* LBR entry structure */
struct dl_lbr_entry {
    u64 from_ip;
    u64 to_ip;
};

/* Function declarations */
int get_host_arch_lbr_depth(void);
int dump_guest_arch_lbr(struct kvm_vcpu *vcpu, struct dl_lbr_entry *entries, int max_entries);
void print_guest_arch_lbr(struct kvm_vcpu *vcpu);
//test
void vmcall_test_dump_lbr(struct kvm_vcpu *vcpu);