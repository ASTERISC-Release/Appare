#ifndef __KVM_X86_VMX_LIBEPT_H
#define __KVM_X86_VMX_LIBEPT_H
#include <linux/list.h>
#include <linux/cma.h>
#include <linux/dma-map-ops.h>
#include "common.h"

#define PT_ENTRIES_PER_PAGE     512
#define _IDX_GVA(_va, _level)       ((_va >> (12 + (_level - 1) * 9)) & ((1 << 9) - 1))
#define _IDX_GFN(_gfn, _level)      ((_gfn >> ((_level - 1) * 9)) & 0x1ff)
#define _IDX_GPA(_gpa, _level)      _IDX_GFN((_gpa >> PAGE_SHIFT), _level)
#define MAP_REQ_FLAGS_SRC_GPA   (1UL << 1)

/* 
 * See Intel Manual (25.6.11, 29.3.2)
 * See Table 29-7 for EPT PTE format
 * https://cdrdv2.intel.com/v1/dl/getContent/671447 
 * 
 */

/*
 * configurable memory type
 * 0: UC
 * 1: WC
 * 4: WT
 * 5: WP
 * 6: WB
 */
#define EPT_MEMORY_TYPE     (6UL)

#define EPT_R               (1UL << 0) // read
#define EPT_W               (1UL << 1) // write
#define EPT_X               (1UL << 2) // exec
#define EPT_MT_SHIFT        (3) // memory type 3-5
#define EPT_MT              (EPT_MEMORY_TYPE << EPT_MT_SHIFT) // memory type
#define EPT_A               (1UL << 8) // accessed
#define EPT_U               (1UL << 10) // user mode exec
#define EPT_ADDR_MASK       (((1UL << 52) - 1) & ~((1UL << 12) - 1)) // 12 to N-1

#define EPT__I              (1UL << 6) // ignore pat memory type
#define EPT__PS             (1UL << 7) // if 1, huge page
#define EPT__A              (1UL << 8) // accessed (if EPT__I is 1)
#define EPT__D              (1UL << 9) // dirty (if EPT__I is 1)
#define EPT_IGN             (1UL << 11)
#define EPT_VERF            (1UL << 57)
#define EPT_WA              (1UL << 58)

#define DL_CR3_ADDR_MASK (((1UL << 63) - 1) & ~((1UL << 12) - 1)) // 12 to M-1

// #define PT_P (1UL << 0) // present
// #define PT_W (1UL << 1) // writable
// #define PT_U (1UL << 2) // user-mode exec
// #define PT_WT (1UL << 3) // write-through
// #define PT_CD (1UL << 4) // cache disable
// #define PT_PS (1UL << 7) // if 1, huge page
//#define PT_ADDR_MASK EPT_ADDR_MASK // same as ept
// #define PT_NX (1UL << 63) // non-executable
#define DEEPLOG_CTX_MAX             512 - 1

extern struct list_head default_EPT_leaf_pages[VCPU_MAX];

/* 
 * `dlctx_ud2_whole_pages`: a list of ud2 pages in the whole page granularity.
 * For each deeplog context, we instantiate a `dlctx_ud2_whole_pages` struct.
 *
 * This structure indicates all the code pages that should be UD2'ed for its 
 * corresponding deeplog context.
 */
typedef struct dlctx_ud2_whole_pages {
    u64 *ud2_pages;
    int nr;
} dlctx_ud2_whole_pgs_t;

/* 
 * `dlctx_ud2_sub_pages`: a list of ud2 pages in the sub page granularity.
 * For each deeplog context, we also instantiate a `dlctx_ud2_sub_pages` struct.
 * 
 * This structure indicates all the code (sub) pages that should be UD2'ed for its
 * corresponding deeplog context, comprised of ([start_0, end_0], ..., [start_nr, end_nr]).
 */
typedef struct dlctx_ud2_sub_pages {
    u64 *ud2_page_starts;
    u64 *ud2_page_ends;
    int nr;
} dlctx_ud2_sub_pgs_t;

typedef struct dlctx_obj_pages {
    u64 *start_address;
    u64 *end_address;
    int nr;
} dlctx_obj_pages_t;


typedef struct deeplog_ctx {
    struct kvm_vcpu *vcpu;
    u64 second_ept_ptr_va;
    u64 second_ept_ptr_pa;
    u64 eptp;             // eptp pointer from ept_ptr_pa
    int active;
    int key;              // deeplog context id
    /* allocated pages for its ept */
    struct list_head leaf_page;
    struct list_head non_leaf_page;
    
    /* UD2 code page information */
    dlctx_ud2_whole_pgs_t ud2_whole_pages;
    dlctx_ud2_sub_pgs_t ud2_sub_pages;
    dlctx_obj_pages_t numa_range;
} deeplog_ctx_t;

extern deeplog_ctx_t deeplog_ctxs[VCPU_MAX][DEEPLOG_CTX_MAX];

/* for each vCPU, we maintain a default context (the system's default EPT) */
extern deeplog_ctx_t deeplog_default_ctxs[VCPU_MAX];

/* vmcall interfaces */
void vmcall_create_init_deeplog_contexts(struct kvm_vcpu *vcpu);
void vmcall_activate_deeplog_contexts(struct kvm_vcpu *vcpu);
void vmcall_setup_eptp_data_protection(struct kvm_vcpu *vcpu,
                                        int ctx_id,
                                        u64 gpa,
                                        int write_protect);
void vmcall_init_obj_contexts(struct kvm_vcpu *vcpu, bool is_cf_enabled);

void vmcall_pin_all_guest_memory(struct kvm_vcpu *vcpu);
void vmcall_walk_through_all_guest_memory_done(struct kvm_vcpu *vcpu);

void vmx_init_ud2_codepage(void);
void vmx_destroy_ud2_codepage(void);

void print_ept_walk(struct kvm_vcpu *vcpu, u64 root, u64 gpa);

void vmx_init_ept_frames(void);
void kvm_reset_ept_frames(void);
void vmx_destroy_ept_frames(void);

void do_ept_gpa_remap(deeplog_ctx_t *deeplog_ctx, u64 src_gpa, u64 dst_hpa,
                      u64 non_leaf_entry_flags, u64 leaf_entry_flags);

void create_deeplog_ctx(deeplog_ctx_t *ctx, struct kvm_vcpu *vcpu, int key);

int is_deeplog_context_init(void);
void set_deeplog_context_init_flag(int flag);

void vmx_init_default_ept_leaf_pages(void);

void kvm_destroy_default_ept_leaf_pages(void);
void kvm_deeplog_ud2_subpage_map_destroy(void);
void kvm_destroy_deeplog_ctxs(void);


u64 deeplog_ud2_map_search(u64 src_gva);
void init_ud2_map(struct kvm_vcpu *vcpu, 
                  u64 __user *src_gva_list, 
                  u64 __user *dst_gva_list, 
                  int nr);
void update_ud2_statistics(struct kvm_vcpu *vcpu, u64 rip, u64 eptp);

u64 *vcpu_eptp_list_fetch(struct kvm_vcpu *vcpu);

// test 
void vmcall_setup_page_wp(struct kvm_vcpu *vcpu, gva_t gva);


// omnilog
void vmcall_omnilog_record_syslog(struct kvm_vcpu *vcpu, u64 src_gva, u64 log_length, u64 syscall_id);
#endif