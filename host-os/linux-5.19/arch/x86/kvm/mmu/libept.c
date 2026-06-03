#include "libept.h"
#include "template.h"
#include "obj_template.h"
/*
 * We reserve this static memory space to serve our EPT tables for different contexts.
 * The current memory size is 1 * (2^20) pages (4096MB).
 * We can adjust this size by changing NBASE later.
 */
// #define VMCALL_OPT /* COMMENT THIS LINE OUT TO DISABLE VMCALL OPTIMIZATION, USEFUL FOR CHECKING UD2's COMPARED TO SYSCALLS */
#define NBASE           1
#define PAGE_ORDER      21

struct page *p_base_pages[NBASE];

void* p_bases[NBASE];
void* p_base;

int p_base_idx;
int p_idx;

bool is_obj_init = false;
EXPORT_SYMBOL(is_obj_init);

/* default EPT */
struct list_head default_EPT_leaf_pages[VCPU_MAX];
EXPORT_SYMBOL(default_EPT_leaf_pages);

/* deeplog contexts */
deeplog_ctx_t deeplog_ctxs[VCPU_MAX][DEEPLOG_CTX_MAX];
EXPORT_SYMBOL(deeplog_ctxs);

/* default contexts (should be initialized during cpu's mmu pgd load) */
deeplog_ctx_t deeplog_default_ctxs[VCPU_MAX];
EXPORT_SYMBOL(deeplog_default_ctxs);

/* context init flag */
static int is_context_init = 0;

int is_deeplog_context_init(void) {
    return is_context_init;
}
EXPORT_SYMBOL(is_deeplog_context_init);

void set_deeplog_context_init_flag(int flag) {
    is_context_init = flag;
}
EXPORT_SYMBOL(set_deeplog_context_init_flag);

void __debug_print_code_bytes(void *start, u32 size) {
    char *idx = start;
    print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1, idx, size, false);
}

/**********************************************************
 * EPT structure-related functions
 **********************************************************/
static gpa_t gva_to_gpa(struct kvm_vcpu *vcpu, u64 gva, struct x86_exception *exception) {
    bool is_kern;
    /* kernel or user address? */
    is_kern = (gva > TASK_SIZE_MAX);
    DL_ASSERT(is_kern == ((gva & (1UL << 47)) != 0));
    if (is_kern)
        return kvm_mmu_gva_to_gpa_system(vcpu, gva, NULL);
    else
        return kvm_mmu_gva_to_gpa_read(vcpu, gva, NULL);
}

static u64 gpa_to_hpa(struct kvm *kvm, u64 gpa)
{
    return gfn_to_pfn(kvm, gpa_to_gfn(gpa)) << PAGE_SHIFT;    
}

static u64 ept_to_eptp(u64 root_hpa, int root_level)
{
    DL_ASSERT(root_hpa != 0);
	/*  0 -   2: value 0 uncacheable, value 6 write-back
	 *  3 -   5: ept walk length - 1
	 *        6: enable access and dirty flags
	 *  7 -  11: reserved
	 * 12 - N-1: root_hpa
	 *  N -  63: reserved
	 */
	return 6UL | ((root_level - 1) << 3) | (1UL << 6) | (root_hpa & EPT_ADDR_MASK);
}

static void* do_alloc_ept_frames (int idx, void **base)
{
    
    // base = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, PAGE_ORDER);
    /* chuqi: replace it with CMA */
    p_base_pages[idx] = dma_alloc_from_contiguous(NULL, (1 << PAGE_ORDER), 
											1, false);
    if (!p_base_pages[idx]) {
        deeplog_log_error("Failed to allocate 2^%d pages for EPT frames.\n",
                            PAGE_ORDER);
        // BUG();
    }
    *base = page_to_virt(p_base_pages[idx]);
    return (*base);
}

void vmx_init_ept_frames(void)
{
    if (!p_base) {
        p_idx = 0;
        p_base_idx = 0;
        p_base = do_alloc_ept_frames(p_base_idx, &p_bases[p_base_idx]);
        deeplog_log_info("Allocate 2^%d pages for EPT frames at pages=0x%lx.\n", 
                        PAGE_ORDER, (unsigned long)p_base_pages[p_base_idx]);
    }
}
EXPORT_SYMBOL(vmx_init_ept_frames);

/* 
 * Reset all allocated EPT page content to 0.
 * Should be invoked after the guest OS clean up (not vmx_exit):
 * x86.c: kvm_arch_destroy_vm
 */
void kvm_reset_ept_frames(void)
{
    int i;
    u64 *p;
    // not incremented base_idx yet
    if (p_base_idx == 0 && p_base_pages[0]){
        DL_ASSERT(p_bases[0] == p_base);
        p = (u64 *)p_bases[0];
        if (p)
            memset(p, 0, PAGE_SIZE * (1 << PAGE_ORDER));
    } else {
        for (i = 0; i < p_base_idx; i++) {
            p = (u64 *)p_bases[i];
            if (p)
                memset(p, 0, PAGE_SIZE * (1 << PAGE_ORDER));
        }
    }
    /* reset idx */
    p_idx = 0;
    p_base_idx = 0;

    deeplog_log_info("[Done] Reset allocated EPT frames.\n");
}
EXPORT_SYMBOL(kvm_reset_ept_frames);

/* 
 * Free all allocated EPT pages.
 * Should be invoked at vmx_exit.
 */
void vmx_destroy_ept_frames(void)
{
    int i;
    // not incremented base_idx yet
    if (p_base_idx == 0 && p_base_pages[0]){
        deeplog_log_info("Free EPT frames pages: 0x%lx.\n", 
                            (unsigned long)p_base_pages[0]);
        dma_release_from_contiguous(NULL, p_base_pages[0], 
                                (1 << PAGE_ORDER));
        p_base_pages[0] = NULL;
        p_bases[0] = 0;
    } else {
        for (i = 0; i < p_base_idx; i++) {
            // free_pages((unsigned long)p_bases[i], PAGE_ORDER);
            if (p_base_pages[i]) {
                deeplog_log_info("Free EPT frames pages: 0x%lx.\n", 
                                (unsigned long)p_base_pages[i]);
                dma_release_from_contiguous(NULL, p_base_pages[i], 
                                        (1 << PAGE_ORDER));
                p_base_pages[i] = NULL;
                p_bases[i] = 0;
            }
        }
    }
    p_base_idx = 0;
    p_base = 0;
    p_idx = 0;
    deeplog_log_info("[Done] Destroy allocated EPT frames.\n");
}
EXPORT_SYMBOL(vmx_destroy_ept_frames);

/**********************************************************
 * EPT-related functions
 **********************************************************/
static u64 *get_ept_page(void)
{
    if (p_base) {
        p_idx++;
        if (p_idx < (1 << PAGE_ORDER)) {
            // int i;
            u64 *p;
            p = (u64 *)((u64)p_base + p_idx * PAGE_SIZE);
            return p;
        }
        else {
            p_base_idx++;
            if (p_base_idx < NBASE) {
                if (!p_bases[p_base_idx])
                    p_base = do_alloc_ept_frames(p_base_idx, &p_bases[p_base_idx]);
                else
                    p_base = p_bases[p_base_idx];
                p_idx = 0;
                return (u64 *)p_base;
            }
            else {
                deeplog_log_error("EPT frames have been used up.\n");
                return 0;
            }
        }
    }
    else {
        deeplog_log_error("EPT base frames have not been allocated.\n");
        return 0;
    }
}

static void *alloc_non_leaf_ept_page(struct list_head *non_leaf_page, int level)
{
    struct kvm_mmu_page *tmp_page;
    void *page;

    tmp_page = kmalloc(sizeof(struct kvm_mmu_page), GFP_KERNEL);
    INIT_LIST_HEAD(&tmp_page->link);
    page = get_ept_page();

    tmp_page->spt = page;
    tmp_page->role.level = level;
    list_add(&tmp_page->link, non_leaf_page);
    return page;
}

static void *alloc_leaf_ept_page(struct list_head *leaf_page, gpa_t gpa)
{
    struct kvm_mmu_page *tmp_page;
    void *page;

    tmp_page = kmalloc(sizeof(struct kvm_mmu_page), GFP_KERNEL);
    INIT_LIST_HEAD(&tmp_page->link);
    page = get_ept_page();

    tmp_page->spt = page;
    tmp_page->role.level = 1;
    /* gfn should be last pte page's whole 512 pages */
    tmp_page->gfn = gpa_to_gfn(gpa) & (~0x1FFUL);
    list_add(&tmp_page->link, leaf_page);
    return page;
}

gpa_t src_gpa, dst_gpa, dst_hpa;


void vmx_init_default_ept_leaf_pages(void) {
    int i;
    for (i = 0; i < VCPU_MAX; i++) {
        INIT_LIST_HEAD(&default_EPT_leaf_pages[i]);
    }
}
EXPORT_SYMBOL(vmx_init_default_ept_leaf_pages);



// chuqi: tmp for counting ept leave pages (evaluation)
static int count_leaf_pages(struct list_head *head)
{
    struct list_head *iter;
    int count = 0;

    list_for_each(iter, head) {
        count++;
    }
    return count;
}
/*
 * chuqi: This function should be invoked while holding the kvm's mmu_lock
 */
static void traverse_all_leaves_with_root(struct kvm_mmu_page *root_sp, struct list_head *leaf_pages)
{
    struct kvm_mmu_page *next_root_sp;
    struct kvm_mmu_page *mmu_page;
    int i;
    u64 *entry_pte;
    /* reach the leaf level */
    if (root_sp->role.level == 1) {
        /* 
         * copy a mmu page, since we can not directly manipulate the default 
         * kvm_mmu_page's link list.
         */
        mmu_page = kmalloc(sizeof(struct kvm_mmu_page), GFP_KERNEL);
        mmu_page->spt = root_sp->spt;
        mmu_page->role.level = 1;
        mmu_page->gfn = root_sp->gfn;
        INIT_LIST_HEAD(&mmu_page->link);
        list_add(&mmu_page->link, leaf_pages);
        return;
    }
    /* recursively iteration for each entry */
    for (i = 0; i < PT_ENTRIES_PER_PAGE; i++) {
        entry_pte = &root_sp->spt[i];
        if (is_shadow_present_pte(*entry_pte)) {
            /* we assume large pte won't happen */
            DL_ASSERT(!is_large_pte(*entry_pte));
            // todo: debug
            // deeplog_log_info("traverse level %d, iter_root (pa 0x%lx[%d]) -> 0x%llx.\n",
            //                 root_sp->role.level, __pa(root_sp->spt), i, *entry_pte);
            /* traverse as the next level page */
            next_root_sp = to_shadow_page(*entry_pte & EPT_ADDR_MASK);
            traverse_all_leaves_with_root(next_root_sp, leaf_pages);
        }
    }
}

static void copy_leaf_ept_ptes(struct list_head *leaf_page, struct kvm_vcpu *vcpu)
{
    struct kvm_mmu_page *page, *ept_mmu_page;
    void *ept_page;
    u64 *sptep;

    int i;
    u64 size = 0;

    /* chuqi:
     * This is useless, since modern KVM maintains TDP (two-dimensional paging)
     */
    // if (list_empty(&vcpu->kvm->arch.active_mmu_pages)){
		// deeplog_log_error("No arch.active_mmu_pages found (since we are using tdp.)!\n");
    // }
    /* 
     * chuqi: 
     * In principle, we are also able to directly use the
     * kvm->arch.tdp_mmu_pages below. 
     * However, I found this structure too late.
     */
    // if (list_empty(&vcpu->kvm->arch.tdp_mmu_pages)){
	// 	deeplog_log_error("No arch.tdp_mmu_pages found!\n");
    // }
    struct list_head *vcpu_default_ept_leaf_pages = &default_EPT_leaf_pages[vcpu->vcpu_id];
    if (list_empty(vcpu_default_ept_leaf_pages)) {
        traverse_all_leaves_with_root(to_shadow_page(vcpu->arch.mmu->root.hpa), vcpu_default_ept_leaf_pages);
        deeplog_log_info("Copy default EPT leaf pages for vcpu_id=%d (page count=%d).\n", 
                            vcpu->vcpu_id, count_leaf_pages(vcpu_default_ept_leaf_pages));
    }
    list_for_each_entry(page, vcpu_default_ept_leaf_pages, link) {
        /* we only copy leaf node entry (SPTEP) mmu_page */
        if (page->role.level == 1) {
            size += 0x1000;
            ept_page = (void *)get_ept_page();
            /* alloc and configure its kvm_mmu_page */
            ept_mmu_page = kmalloc(sizeof(struct kvm_mmu_page), GFP_KERNEL);
            ept_mmu_page->spt = ept_page;    // set as its root page
            ept_mmu_page->role.level = 1;
            ept_mmu_page->gfn = page->gfn;
            INIT_LIST_HEAD(&ept_mmu_page->link);
            list_add(&ept_mmu_page->link, leaf_page);

            // todo: should we add to mmu_page_hash for accelaration?
            // hlist_add_head(&ept_mmu_page->hash_link, arch->mmu_page_hash[kvm_page_table_hashfn(page->gfn)]);
            sptep = (u64 *)ept_page;
            for (i = 0; i < PT_ENTRIES_PER_PAGE; i++) {
                sptep[i] = page->spt[i];  // simply copy last ept pte value
            }
        }
        /* should NOT walked huge page entries !!! */
        if (page->role.level == 2 || page->role.level == 3) {
            for (i = 0; i < PT_ENTRIES_PER_PAGE; i++) {
                if (is_large_pte(page->spt[i])) {
                    deeplog_log_error("Huge page entries occurred. level: %d, spte: 0x%llx.\n",
                                        page->role.level, page->spt[i]);
                }
            }
        }
    }

  deeplog_log_info("copy_leaf page size: %llx", size);
}

static u64 construct_ept(deeplog_ctx_t *deeplog_ctx, u64 non_leaf_entry_flags)
{
    u64 *root, *next_entry, *iter_root;
    u64 size;
    // u64 *pdpt, *pd;
    struct kvm_mmu_page *root_page, *cur_page;
    // int pml4_ind, pdpt_ind, pd_ind;
    int i, entry_idx; 
    struct list_head *leaf_page, *non_leaf_page;

    leaf_page = &deeplog_ctx->leaf_page;
    non_leaf_page = &deeplog_ctx->non_leaf_page;
    

    size = 0x1000;
    root_page = kmalloc(sizeof(struct kvm_mmu_page), GFP_KERNEL);
    root_page->spt = (u64 *)get_ept_page();
    root_page->role.level = 4;

    INIT_LIST_HEAD(&root_page->link);
    list_add(&root_page->link, non_leaf_page);

    root = root_page->spt;

    /* default flags */
    if (!non_leaf_entry_flags) {
        non_leaf_entry_flags = EPT_R | EPT_W | EPT_X | EPT_A | EPT_IGN;
    } 

    list_for_each_entry(cur_page, leaf_page, link) {
        iter_root = root;
        /* start hierarchial page table walk */
        for (i = root_page->role.level; i > 1; i--) {
            entry_idx = _IDX_GFN(cur_page->gfn, i); 
            /* no entry page allocated, we create its next-level entry page */
            if (i > 2) {
                if (! iter_root[entry_idx]) {
                    size += 0x1000;
                    next_entry = (u64 *)alloc_non_leaf_ept_page(non_leaf_page, i - 1);
                    iter_root[entry_idx] = __pa(next_entry) | non_leaf_entry_flags;
                } else {
                    next_entry = __va(iter_root[entry_idx] & EPT_ADDR_MASK); 
                }
                iter_root = next_entry;
            } 
            else {   // pde -> ptep (we already copied leaf page's ept entry page)
                if (! iter_root[entry_idx]) {
                    iter_root[entry_idx] = __pa(cur_page->spt) | non_leaf_entry_flags;
                }
            }
        }
    }
    deeplog_ctx->second_ept_ptr_pa = (u64)__pa(root);
    deeplog_ctx->second_ept_ptr_va = (u64)phys_to_virt((deeplog_ctx->second_ept_ptr_pa));
    deeplog_ctx->eptp = ept_to_eptp(deeplog_ctx->second_ept_ptr_pa, 4);
    
    // debug
    deeplog_log_info("Create secondary EPT for context id=%d, at EPTP=0x%llx.\n",
                    deeplog_ctx->key, deeplog_ctx->eptp);
    deeplog_log_info("size of ept here: %llx", size);
    return deeplog_ctx->second_ept_ptr_pa;
}

deeplog_ctx_t *fetch_ctx(struct kvm_vcpu * vcpu, u64 eptp){
  int i;
  for (i = 0; i < DEEPLOG_CTX_MAX; i++){
    // found
    if ( deeplog_ctxs[vcpu->vcpu_id][i].eptp == eptp ) {
      return &deeplog_ctxs[vcpu->vcpu_id][i];
    }
    /*
    deeplog_log_info("TGT eptp: 0x%llx; found ctx.eptp: 0x%llx.\n",
                     eptp, deeplog_ctxs[vcpu->vcpu_id][i].eptp);
                     */
  }
  return NULL;
}
EXPORT_SYMBOL(fetch_ctx);

/*
 * src_gpa: the source GPA
 * 
 * non_leaf_entry_flags: 
 *  the flags for non-leaf entry (`0` is default: R/W/X/A/IGN)
 *  if `1`: then remove write_access flag
 * 
 * leaf_entry_flags: 
 *  the flags for leaf entry (`0` is default: R/W/X/MT/IGN/VERF/WA)
 *  if `1`: then remove write_access flag
 */
void do_ept_gpa_remap(deeplog_ctx_t *deeplog_ctx, u64 src_gpa, u64 dst_hpa,
                             u64 non_leaf_entry_flags, u64 leaf_entry_flags)
{
    u64 *ept_root, *next_entry, *iter_root;
    int i, entry_idx;
    struct list_head *leaf_page, *non_leaf_page;

    leaf_page = &deeplog_ctx->leaf_page;
    non_leaf_page = &deeplog_ctx->non_leaf_page;
    ept_root = (u64 *)(deeplog_ctx->second_ept_ptr_va);

    /* set up default flags */
    if (!non_leaf_entry_flags) {
        /* bits: 0 | 1 | 2 | 8 | 11 */
        non_leaf_entry_flags = EPT_R | EPT_W | EPT_X | EPT_A | EPT_IGN;
    } else if (non_leaf_entry_flags == 1) {
        non_leaf_entry_flags = EPT_R | EPT_X | EPT_A | EPT_IGN;
    }
    
    if (!leaf_entry_flags) {
        /* bits: 0 | 1 | 2 | (bits 5-3: 110b) | 6 | 8 | 9 | 11 | 57 | 58 */
        leaf_entry_flags = EPT_R | EPT_W | EPT_X | EPT_MT;
        leaf_entry_flags |= EPT__I | EPT__A | EPT__D | EPT_IGN;
        leaf_entry_flags |= EPT_VERF | EPT_WA;
    } else if (leaf_entry_flags == 1) {
        leaf_entry_flags = EPT_R | EPT_X | EPT_MT;
        leaf_entry_flags |= EPT__I | EPT__A | EPT__D | EPT_IGN;
        leaf_entry_flags |= EPT_VERF | EPT_WA;
    }

    iter_root = ept_root;
    for (i = 4; i > 1; i--) {
        entry_idx = _IDX_GPA(src_gpa, i);
        if (i > 2) {
            /* no entry page allocated, we create its next-level entry page */
            if ( !iter_root[entry_idx]) {
                next_entry = (u64 *)alloc_non_leaf_ept_page(non_leaf_page, i - 1);
                iter_root[entry_idx] = __pa(next_entry) | non_leaf_entry_flags;
            } else {
                next_entry = __va(iter_root[entry_idx] & EPT_ADDR_MASK);
            }
            iter_root = next_entry;
        }
        /* assign the pde -> ptep leaf page */
        else {
            if (! iter_root[entry_idx]) {
                next_entry = (u64 *)alloc_leaf_ept_page(leaf_page, src_gpa);
                iter_root[entry_idx] = __pa(next_entry) | non_leaf_entry_flags;
            } else {
                next_entry = __va(iter_root[entry_idx] & EPT_ADDR_MASK);
            }
            iter_root = next_entry;
        }
    }
    /* leaf entry (epte -> hpa) */
    entry_idx = _IDX_GPA(src_gpa, 1);
    iter_root[entry_idx] = dst_hpa | leaf_entry_flags;
}
EXPORT_SYMBOL(do_ept_gpa_remap);


/**********************************************************
 * Memory cleanup (destory) functions
 **********************************************************/
void kvm_destroy_default_ept_leaf_pages(void)
{
    int i;
    struct list_head *vcpu_default_ept_leaf_pages;
     
    for (i = 0; i < VCPU_MAX; i++) {
        vcpu_default_ept_leaf_pages = &default_EPT_leaf_pages[i];

        if (!list_empty(vcpu_default_ept_leaf_pages)) {
            struct kvm_mmu_page *cur_page, *n;
            list_for_each_entry_safe(cur_page, n, vcpu_default_ept_leaf_pages, link) {
                list_del(&cur_page->link);
                kfree(cur_page);
            }
        }

        memset(vcpu_default_ept_leaf_pages, 0, sizeof(struct list_head));
    }
    deeplog_log_info("[Done] Destroy default EPT leaf pages.\n");
}
EXPORT_SYMBOL(kvm_destroy_default_ept_leaf_pages);


/**********************************************************
 * vmcall handlers
 **********************************************************/
void print_ept_walk(struct kvm_vcpu *vcpu, u64 root, u64 gpa)
{
    struct kvm_shadow_walk_iterator iterator;
    u64 spte, *sptep, new_gpa;
    new_gpa = gpa;
    // use default ept root
    if (!root)
        root = vcpu->arch.mmu->root.hpa;
    deeplog_log_info("Walk (GPA: 0x%llx, HPA: 0x%llx) with root EPT: 0x%llx.\n", 
                    gpa, gpa_to_hpa(vcpu->kvm, gpa), root);

    write_lock(&vcpu->kvm->mmu_lock);
    for_each_shadow_entry_using_root(vcpu, root, new_gpa, iterator) {
		sptep = iterator.sptep;
        spte = *sptep;
        deeplog_log_info("Walk level: %d, sptep: 0x%lx pa: 0x%lx -> spte: 0x%lx.\n", 
                        iterator.level, (unsigned long)sptep, __pa(sptep), (unsigned long)spte);
    }
    write_unlock(&vcpu->kvm->mmu_lock);
}
EXPORT_SYMBOL(print_ept_walk);

u64 gaddr_to_ept_spte(struct kvm_vcpu *vcpu, u64 guest_addr, bool is_gpa)
{
    gpa_t gpa;
    hpa_t hpa;
    // u64 *sptep, spte, old_spte;
    // gfn_t gfn;
    // struct kvm_mmu_page *sp;
    struct kvm_mmu_page *root;
    // struct kvm_shadow_walk_iterator iterator;
    // int level;
    // int walk_level;

    if (is_tdp_mmu(vcpu->arch.mmu)) {
		deeplog_log_info("TDP MMU.\n");
        root = to_shadow_page(vcpu->arch.mmu->root.hpa);
        deeplog_log_info("EPT root level: %d.\n", root->role.level);
    }
	else
		deeplog_log_info("Shadow MMU.\n");
    
    if (is_gpa)
        gpa = (gpa_t)guest_addr;
    else
        gpa = gva_to_gpa(vcpu, guest_addr, NULL);

    hpa = gpa_to_hpa(vcpu->kvm, gpa);
    if (is_gpa)
        deeplog_log_info("Get HPA: 0x%llx <-> GPA: 0x%llx.\n", hpa, gpa);
    else
        deeplog_log_info("Get HPA: 0x%llx <-> GPA: 0x%llx <-> GVA: 0x%llx.\n", 
                        hpa, gpa, guest_addr);
    
    print_ept_walk(vcpu, 0, gpa);
    return 0; 
}
EXPORT_SYMBOL(gaddr_to_ept_spte);

/**
 * Initialize the UD2 handling map. 
 * If `dst_gva_list` is NULL (this is for deployment):
 * We map each GVA inside the src_gva_list, to mark its GPA -> HPA and remap those GPA to a UD2 code page.
 * When an UD2 is encountered, we then switch back its original GPA -> HPA mapping.
 * 
 * If `dst_gva_list` is provided (this is for testing):
 * When an UD2 is encountered, we should remap each src_gva's GVA -> GPA --> dst_gva's HPA.
 */

/* should be initialized to UD2 during vmx_init */
// char ud2_codepage[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

struct page *ud2_codepage;
u64 ud2_codepage_hva;
u64 ud2_codepage_hpa;

void vmx_init_ud2_codepage(void) 
{
    char *pg_idx, *pg_end; 
    /* assign a code page */
    DL_ASSERT(!ud2_codepage);
    DL_ASSERT(!ud2_codepage_hva);
    DL_ASSERT(!ud2_codepage_hpa);
    ud2_codepage = alloc_page(GFP_KERNEL | __GFP_ZERO);
    ud2_codepage_hva = (u64)page_to_virt(ud2_codepage);
    ud2_codepage_hpa = (u64)page_to_phys(ud2_codepage);

    pg_idx = (char *)ud2_codepage_hva;
    pg_end = pg_idx + PAGE_SIZE;
    /* UD2: 0x0f 0x0b */
    while (pg_idx != pg_end) {
        *pg_idx = (char)0x0f;
        pg_idx += 1;
        if (pg_idx == pg_end)
            break;
        *(pg_idx) = (char)0x0b;
        pg_idx += 1;
    }

    deeplog_log_info("UD2 code page initialized at HVA: 0x%llx -- HPA: 0x%llx.\n",
                    (u64)ud2_codepage_hva, (u64)ud2_codepage_hpa);
}
EXPORT_SYMBOL(vmx_init_ud2_codepage);

void vmx_destroy_ud2_codepage(void)
{
    DL_ASSERT(ud2_codepage);
    DL_ASSERT(ud2_codepage_hva);
    DL_ASSERT(ud2_codepage_hpa);
    
    __free_page(ud2_codepage);
    ud2_codepage = 0;
    ud2_codepage_hva = 0;
    ud2_codepage_hpa = 0;
    deeplog_log_info("[Done] Destroy UD2 code page.\n");
}
EXPORT_SYMBOL(vmx_destroy_ud2_codepage);

struct hlist_head deeplog_ud2_map_table[];
DEFINE_HASHTABLE(deeplog_ud2_map_table, 8);

struct hlist_head deeplog_ud2_subpage_map_table[];
DEFINE_HASHTABLE(deeplog_ud2_subpage_map_table, 8);

typedef struct deeplog_ud2_hash_entry {
    u64 src_gva;   // gva page is the key (code page)
    u64 dst_hpa;   // hpa page is the target mapping
    struct hlist_node hlist;
} deeplog_ud2_hash_entry_t;


typedef struct deeplog_ud2_subpage_hash_entry {
    int ctx_id;
    u64 src_gpa;   // gpa page corresponding to the given GVA subpage range
    u64 dst_hpa;   // hpa page (code page with subpage UD2) is the target mapping
    bool remapped; 
    struct page *page;   // used for free later
    struct hlist_node hlist;
} deeplog_ud2_subpage_hash_entry_t;

static u64 deeplog_ud2_subpage_key_hash(int ctx_id, u64 src_gpa)
{
    return hash_64(ctx_id ^ src_gpa, 8);
}

u64 deeplog_ud2_map_search(u64 src_gva)
{
    deeplog_ud2_hash_entry_t *entry;
    hash_for_each_possible(deeplog_ud2_map_table, entry, hlist, src_gva) {
        if (entry->src_gva == src_gva) {
            return entry->dst_hpa;
        }
    }
    return 0;
}
EXPORT_SYMBOL(deeplog_ud2_map_search);

/*
 * Given a GVA page with (src_gva_start, src_gva_end) range, a.k.a, `dlctx_ud2_sub_pgs_t`, 
 * we create a new page with UD2 replacing this range, and return this page's HPA.
 */
deeplog_ud2_subpage_hash_entry_t *deeplog_ud2_subpage_map_fetch(struct kvm_vcpu *vcpu, deeplog_ctx_t *ctx,
                                                                u64 src_gva_start, u64 src_gva_end)
{
    deeplog_ud2_subpage_hash_entry_t *entry;
    u64 hash_key;
    struct page *new_page;
    u64 page_virt, page_phys;
    
    char *pg_startoff, *pg_idx, *pg_end;
    u32 bytes;
    int r, found;
    u64 src_gpa;
    
    gva_t guest_page_virt = src_gva_start & PAGE_MASK;
    /* guest_page will always be a kernel VA page, so we use `kvm_mmu_gva_to_gpa_system` */
    src_gpa = kvm_mmu_gva_to_gpa_system(vcpu, guest_page_virt, NULL);

    hash_key = deeplog_ud2_subpage_key_hash(ctx->key, src_gpa);
    found = 0;

    /* such HPA with deeplog context is already recorded in the hash map */
    hash_for_each_possible(deeplog_ud2_subpage_map_table, entry, hlist, hash_key) {
        if (entry->ctx_id == ctx->key && entry->src_gpa == src_gpa) {
            // debug
            // deeplog_log_info("[ctx=%d] Found subpage gva_range: (0x%llx - 0x%llx) => {src_gpa: 0x%llx, dst_hpa: 0x%llx.}.\n", 
            //                     ctx->key, src_gva_start, src_gva_end, entry->src_gpa, entry->dst_hpa);
            found = 1;
            break;
        }
    }

    if (!found) {
        /* no such HPA page yet, we create a new one and insert it into the hashmap */
        new_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
        page_virt = (u64)page_to_virt(new_page);
        page_phys = (u64)page_to_phys(new_page);

        /* create an entry */
        entry = (deeplog_ud2_subpage_hash_entry_t *)kmalloc(
                            sizeof(deeplog_ud2_subpage_hash_entry_t), GFP_KERNEL | __GFP_ZERO);
        entry->ctx_id = ctx->key;
        entry->src_gpa = src_gpa;
        entry->dst_hpa = page_phys;
        entry->page = new_page;
        entry->remapped = false;
        /* insert the entry into the hashmap */
        hash_add(deeplog_ud2_subpage_map_table, &entry->hlist, hash_key);
        
        // debug
        // deeplog_log_info("[ctx=%d] Add subpage gva_range: (0x%llx - 0x%llx) => {src_gpa: 0x%llx, dst_hpa: 0x%llx.}.\n", 
        //                     ctx->key, src_gva_start, src_gva_end, entry->src_gpa, entry->dst_hpa);
        
        /* read the guest's raw code page content to the `new_page` */
        r = kvm_vcpu_read_guest(vcpu, entry->src_gpa, (void *)page_virt, PAGE_SIZE);
        if (r < 0) {
            deeplog_log_error("Failed to read guest page content from GVA: 0x%llx to HVA: 0x%llx.\n", 
                                (u64)guest_page_virt, page_virt);
            BUG();
        } 
    }
    else {
        page_virt = (u64)page_to_virt(entry->page);
        page_phys = entry->dst_hpa;
        DL_ASSERT(page_phys == (u64)page_to_phys(entry->page));
    }
    /* replace the subpage range with UD2 */
    bytes = src_gva_end - src_gva_start;
    DL_ASSERT(bytes <= PAGE_SIZE);
    
    pg_startoff = (char *)page_virt + (src_gva_start & (PAGE_SIZE-1));
    pg_idx = pg_startoff;
    pg_end = pg_startoff + bytes;
    // deeplog_log_info("Replace subpage range HVA: 0x%llx - 0x%llx with UD2.\n", 
    //                     (u64)pg_idx, (u64)pg_end); 
    while (pg_idx != pg_end) {
        *pg_idx = (char)0x0f;
        pg_idx += 1;
        if (pg_idx == pg_end)
            break;
        *(pg_idx) = (char)0x0b;
        pg_idx += 1;
    }
    // debug: will flood the dmesg!
    // __debug_print_code_bytes((void *)page_virt, 0x1000);
    return entry;
}

/* destroy the hash map */
void kvm_deeplog_ud2_subpage_map_destroy(void)
{
    unsigned bkt;
    deeplog_ud2_subpage_hash_entry_t *entry;
    struct hlist_node *tmp;

    /* iterate all table */
    hash_for_each_safe(deeplog_ud2_subpage_map_table, bkt, tmp, entry, hlist) {
        hash_del(&entry->hlist);
        if (entry->page) {
            DL_ASSERT(entry->dst_hpa == (u64)page_to_phys(entry->page));
            __free_page(entry->page);
        }
        kfree(entry);
    }
    deeplog_log_info("[Done] Destroy deeplog_ud2_subpage_map_table.\n");
}
EXPORT_SYMBOL(kvm_deeplog_ud2_subpage_map_destroy);

void init_ud2_map(struct kvm_vcpu *vcpu, u64 __user *src_gva_list, u64 __user *dst_gva_list, int nr)
{
    int i;
    u64 temp_gva, temp_gpa, src_gva, src_gpa, dst_hpa;
    struct x86_exception e;
    deeplog_ud2_hash_entry_t *entry;

    temp_gpa = 0;
    for (i = 0; i < nr; i++) {
        kvm_read_guest_virt(vcpu, (gva_t)(src_gva_list + i), &src_gva, sizeof(u64), &e);
        src_gpa = kvm_mmu_gva_to_gpa_system(vcpu, src_gva, NULL);
        if (!dst_gva_list) {
            dst_hpa = gpa_to_hpa(vcpu->kvm, src_gpa);
        }
        else {
            kvm_read_guest_virt(vcpu, (gva_t)(dst_gva_list + i), &temp_gva, sizeof(u64), &e);
            temp_gpa = kvm_mmu_gva_to_gpa_system(vcpu, temp_gva, NULL);
            dst_hpa = gpa_to_hpa(vcpu->kvm, temp_gpa);
        }

        entry = (deeplog_ud2_hash_entry_t *)kmalloc(sizeof(deeplog_ud2_hash_entry_t), GFP_KERNEL);
        if (entry) {
            entry->src_gva = src_gva;
            entry->dst_hpa = dst_hpa;
            hash_add(deeplog_ud2_map_table, &entry->hlist, entry->src_gva);
        }

        // debug print the default EPT walk
        deeplog_log_info("debug print src_gva: 0x%llx src_gpa: 0x%llx default EPT walk.\n",
                             src_gva, src_gpa);
        print_ept_walk(vcpu, 0, src_gpa);
        if (temp_gpa) {
            deeplog_log_info("debug print (tmp) dst_gva: 0x%llx, dst_gpa: 0x%llx default EPT walk.\n",
                                 temp_gva, temp_gpa);
            print_ept_walk(vcpu, 0, temp_gpa);
        }
    }
}
EXPORT_SYMBOL(init_ud2_map);


u64 total_ud2_number;
EXPORT_SYMBOL(total_ud2_number);
void update_ud2_statistics(struct kvm_vcpu *vcpu, u64 rip, u64 eptp)
{
    int ctx_id, i;
    u64 *eptp_list = vcpu_eptp_list_fetch(vcpu);
    /* get current deeplog_ctx id */
    ctx_id = -1;
    for (i = 0; i < 511; i++) {
        if (eptp == eptp_list[i]) {
            ctx_id = i;
            break;
        }
    }
    /* update total_ud2_number */
    total_ud2_number += 1;

    /* print? */
    deeplog_log_info("[UD2][vcpu=%d, ctx=%d] RIP (UD2 point) virt=0x%llx phys=0x%llx. current total number: 0x%llx.\n",
    vcpu->vcpu_id, ctx_id, rip, gva_to_gpa(vcpu, rip, NULL), total_ud2_number);
    

}
EXPORT_SYMBOL(update_ud2_statistics);

int obj_profile[500][500];
EXPORT_SYMBOL(obj_profile);

int obj_violations[VCPU_MAX];
EXPORT_SYMBOL(obj_violations);

void update_obj_profile(int syscall, int obj_id) {
//   printk(KERN_INFO "update_obj_profile: syscall=%d, obj_id=%d\n", syscall, obj_id);
  if (syscall >= 500 || obj_id >= 500)
    return;
  obj_profile[syscall][obj_id] = 1;
}
EXPORT_SYMBOL(update_obj_profile);

/**********************************************************
 * Deeplog context-related functions
 **********************************************************/

// Chuqi: Hand waivy mask schedules/interrupts/syscall_entries
// Future refer to vmlinux.lds.S to automatically figure out those sections
unsigned long white_function_pages[] =  {
    0xffffffff81c8d000,
    0xffffffff81c8e000,
    0xffffffff81c8f000,
    0xffffffff81c90000,
    0xffffffff81c91000,
    0xffffffff81c92000,
    0xffffffff81c93000,
    0xffffffff81c94000, // sched_text_
    0xffffffff81c95000,
    0xffffffff81c96000,
    0xffffffff81c97000,
    0xffffffff81c98000,
    0xffffffff81c99000,
    0xffffffff81c9a000,
    0xffffffff81c9b000,
    0xffffffff81c9c000, // __lock_text
    0xffffffff81c9d000, 
    0xffffffff81e00000, // __entry_text
    0xffffffff81e01000, // __irqentry_text
    0xffffffff81e02000,
    0xffffffff82000000,
    0xffffffff82001000,
};

#define WHITELIST_SIZE (sizeof(white_function_pages) / sizeof(white_function_pages[0]))

static int page_in_whitelist(unsigned long page_va) {
    int i;
    for (i = 0; i < WHITELIST_SIZE; i++) {
        if (page_va == white_function_pages[i])
            return 1;
    }
    return 0;
}

static int subpage_in_whitelist(unsigned long subsec_start, unsigned long subsec_end) {
    int i;
    unsigned long pg_start, pg_end;
    for (i = 0; i < WHITELIST_SIZE; i++) {
        pg_start = white_function_pages[i];
        pg_end = pg_start + 0x1000;
        if (subsec_start >= pg_start && subsec_start < pg_end)
            return 1;
        if (subsec_end >= pg_start && subsec_end <= pg_end)
            return 1;
    }
    return 0;
}

void create_deeplog_ctx(deeplog_ctx_t *ctx, struct kvm_vcpu *vcpu, int key)
{
    ctx->vcpu = vcpu;
    ctx->active = 1;
    ctx->key = key;
    ctx->second_ept_ptr_va = 0;
    ctx->second_ept_ptr_pa = 0;
    ctx->eptp = 0;
    INIT_LIST_HEAD(&ctx->leaf_page);
    INIT_LIST_HEAD(&ctx->non_leaf_page);

    ctx->ud2_whole_pages = (dlctx_ud2_whole_pgs_t){};
    ctx->ud2_sub_pages = (dlctx_ud2_sub_pgs_t){};
}
EXPORT_SYMBOL(create_deeplog_ctx);

static int __deeplog_ctx_has_secondary_ept(deeplog_ctx_t *ctx)
{
    return ctx->active && (ctx->ud2_sub_pages.nr || ctx->ud2_whole_pages.nr);
}

/* 
 * This function should be called after `create_deeplog_ctx` 
 */
static int subpage_in_wholepage(deeplog_ctx_t *ctx, u64 subpage_start)
{
    int i;
    u64 wholepage_vpfn, subpage_vpfn;

    for (i = 0; i < ctx->ud2_whole_pages.nr; i++) {
        wholepage_vpfn = (ctx->ud2_whole_pages.ud2_pages[i]) >> PAGE_SHIFT;
        subpage_vpfn = subpage_start >> PAGE_SHIFT;
    
        if (subpage_vpfn == wholepage_vpfn) {
            return 1;
        }
    }
    return 0;
}

void init_deeplog_ctx(deeplog_ctx_t *ctx, struct kvm_vcpu *vcpu)
{
    int ctx_id, i;
    u64 guest_wholepage_va, guest_wholepage_pa;

    u64 guest_subpage_start, guest_subpage_end;
    deeplog_ud2_subpage_hash_entry_t *subpage_entry;
    
    ctx_id = ctx->key;
    i = 0;
    /* initialize context UD2 pages */
    DEEPLOG_TEMPLATE_INIT(ctx, ctx_id); 

    /* create it's own EPT */
    if (ctx->ud2_whole_pages.nr || ctx->ud2_sub_pages.nr) {
        write_lock(&vcpu->kvm->mmu_lock);
        /* copy current vcpu's default EPT leaf pages */
        copy_leaf_ept_ptes(&ctx->leaf_page, vcpu);
        construct_ept(ctx, 0);
        write_unlock(&vcpu->kvm->mmu_lock);

        /* handle fullpage to UD2 */
        if (!ud2_codepage_hpa) {
            deeplog_log_error("UD2 code page has not been initialized.\n");
            BUG();
        }

        for (i = 0; i < ctx->ud2_whole_pages.nr; i++) {
            guest_wholepage_va = ctx->ud2_whole_pages.ud2_pages[i];
            guest_wholepage_pa = kvm_mmu_gva_to_gpa_system(vcpu, guest_wholepage_va, NULL);
            // TODO(): SOFTPATCH for 0xffffffff82000000 interruption page (asm_exc_page_fault)
            // TODO(): SOFTPATCH for 0xffffffff81e7c000 syscall_entry page (entry_SYSCALL_64)
            
            // if (guest_wholepage_va == 0xffffffff82000000 ||
            //     guest_wholepage_va == 0xffffffff81e7c000) {
            //     continue;
            // }
            
            if (page_in_whitelist(guest_wholepage_va)) {
                continue;
            }

            /* remap this entry GPA -> HPA inside it's EPT */
            do_ept_gpa_remap(ctx, guest_wholepage_pa, ud2_codepage_hpa, 0, 0);
            // debug
            // deeplog_log_info("Context=%d, remap GVA: 0x%llx -- GPA: 0x%llx --> HPA: 0x%llx.\n",
            //         ctx->key, guest_wholepage_va, guest_wholepage_pa, ud2_codepage_hpa);
        }

        /* handle subpage to UD2 */
        for (i = 0; i < ctx->ud2_sub_pages.nr; i++) {
            guest_subpage_start = ctx->ud2_sub_pages.ud2_page_starts[i];
            guest_subpage_end = ctx->ud2_sub_pages.ud2_page_ends[i];

            /* determine whether we already mapped the full page for this sub-region */
            if (subpage_in_wholepage(ctx, guest_subpage_start)) {
                continue;
            }

            // TODO(): SOFTPATCH for 0xffffffff82000000 interruption page (asm_exc_page_fault)
            // TODO(): SOFTPATCH for 0xffffffff81e7c000 syscall_entry page (entry_SYSCALL_64)
            // if (guest_subpage_start >= 0xffffffff82000000 && guest_subpage_end <= 0xffffffff82001000) {
            //     continue;
            // }
            // if (guest_subpage_start >= 0xffffffff81e7c000 && guest_subpage_end <= 0xffffffff81e7d000) {
            //     continue;
            // }
            if (subpage_in_whitelist(guest_subpage_start, guest_subpage_end)) {
                continue;
            }

            subpage_entry = deeplog_ud2_subpage_map_fetch(vcpu, ctx, guest_subpage_start, guest_subpage_end);
            /* remap this entry GPA -> HPA inside it's EPT */
            if (!subpage_entry->remapped) {
                do_ept_gpa_remap(ctx, subpage_entry->src_gpa, subpage_entry->dst_hpa, 0, 0);
                subpage_entry->remapped = true;
            } 
        }
    }
    /* 
     * no specific UD2 for the context, then we simply reuse
     * the default EPT for this context.
     * 
     * that is, it's not necessary to create a secondary ept 
     * for that context.
     */
    else {/* do nothing */}
}

u64 FIRST_NUMA_NODE_BEGIN = 0x00000001d1c00000;
u64 END_OF_NUMA_REGION = 0x0000000233ffffff;
// u64 END_OF_NUMA_REGION = 0x0000000233ffc00f;
// u64 END_OF_NUMA_REGION = 0x0000000233bfffff;
// u64 END_OF_NUMA_REGION = 0x0000000205c00000;       // 210 objects?
void init_deeplog_ctx_obj(deeplog_ctx_t *ctx, struct kvm_vcpu *vcpu, bool is_cf_enabled) {
  int ctx_id, i;
  u64 guest_wholepage_pa;

  ctx_id = ctx->key;
  i = 0;
  /* TODO: create my own DEEPLOG_TEMPLATE_INIT for objects */
  if (!is_cf_enabled)
    DEEPLOG_TEMPLATE_INIT(ctx, ctx_id);  /* We will use this for profiling */
  DEEPLOG_OBJ_TEMPLATE_INIT(ctx);


  /* create it's own EPT */
  /* This is kind of hacky, I don't have enough memory to create an EPT for each system call. 
   * So instead I utilize the syscalls inside our CF profile to determine whether or not we should make an EPT :) */
  if (ctx->ud2_whole_pages.nr || ctx->ud2_sub_pages.nr) {
    if (!is_cf_enabled) {
      write_lock(&vcpu->kvm->mmu_lock);
      /* copy current vcpu's default EPT leaf pages */
      copy_leaf_ept_ptes(&ctx->leaf_page, vcpu);
      construct_ept(ctx, 0);
      write_unlock(&vcpu->kvm->mmu_lock);
    }

#if 1 /* HACKY OBJECT PROFILING CODE, sorry chuqi :) */
    for (guest_wholepage_pa = FIRST_NUMA_NODE_BEGIN; guest_wholepage_pa < END_OF_NUMA_REGION; guest_wholepage_pa += 0x1000) {
      // do_ept_gpa_remap(ctx, guest_wholepage_pa, gpa_to_hpa(vcpu->kvm, guest_wholepage_pa), 0, 0);
      do_ept_gpa_remap(ctx, guest_wholepage_pa, gpa_to_hpa(vcpu->kvm, guest_wholepage_pa), 0, 1);
    }
#endif
#if 0
    for (i = 0; i < ctx->numa_range.nr; i++) {
      deeplog_log_info("Context=%d, remap 0x%llx through 0x%llx.\n",
        ctx->key, ctx->numa_range.start_address[i], ctx->numa_range.end_address[i]);
      for (guest_wholepage_pa = ctx->numa_range.start_address[i]; guest_wholepage_pa < ctx->numa_range.end_address[i]; guest_wholepage_pa += 0x1000) {
        /* change permissions (remove write) on the current page */
        do_ept_gpa_remap(ctx, guest_wholepage_pa, gpa_to_hpa(vcpu->kvm, guest_wholepage_pa), 0, 1);
      }
    }
#endif
  }
}

void vmcall_init_obj_contexts(struct kvm_vcpu *vcpu, bool is_cf_enabled) {
  int i, ctx_id;
  unsigned long vcpu_id;
  deeplog_ctx_t *ctx;
  
  struct kvm *kvm;
  struct kvm_vcpu *vcpu_iter;

  /* make vcpu id aware */
  //   nr_vcpus = atomic_read(&kvm->online_vcpus);
  kvm = vcpu->kvm;

  /* create for all vcpus */
  kvm_for_each_vcpu(vcpu_id, vcpu_iter, kvm) {
    deeplog_log_info("Create deeplog obj contexts for vcpu_id: %lu.\n", vcpu_id);
    for (i = 0; i < DEEPLOG_CTX_MAX; i++) {
        ctx = &deeplog_ctxs[vcpu_id][i];
        ctx_id = i;
        /* create */
        if (!is_cf_enabled)
          create_deeplog_ctx(ctx, vcpu_iter, ctx_id);
        /* then init contexts for object profiling (for now) */
        /* if you're reading this I either failed horribly or forgot to update this comment */
        init_deeplog_ctx_obj(ctx, vcpu_iter, is_cf_enabled);
    }
  }
}
EXPORT_SYMBOL(vmcall_init_obj_contexts);

u64 vmcall_addr = 0xffffffff81e7c26d;
void write_nop_to_guest_syscall(struct kvm_vcpu *vcpu) {
#ifndef VMCALL_OPT
  return;
#else
  kvm_vcpu_write_guest(vcpu, kvm_mmu_gva_to_gpa_system(vcpu, vmcall_addr, NULL), "\x90\x90\x90", 3);
#endif
}
EXPORT_SYMBOL(write_nop_to_guest_syscall);

void write_vmcall_to_guest_syscall(struct kvm_vcpu *vcpu) {
#ifndef VMCALL_OPT
  return;
#else
  kvm_vcpu_write_guest(vcpu, kvm_mmu_gva_to_gpa_system(vcpu, vmcall_addr, NULL), "\x0f\x01\xc1", 3);
#endif 
}
EXPORT_SYMBOL(write_vmcall_to_guest_syscall);

void vmcall_create_init_deeplog_contexts(struct kvm_vcpu *vcpu)
{
  int i, ctx_id;
  unsigned long vcpu_id;
  deeplog_ctx_t *ctx;
  
  struct kvm *kvm;
  struct kvm_vcpu *vcpu_iter;

  /* make vcpu id aware */
  //   nr_vcpus = atomic_read(&kvm->online_vcpus);
  kvm = vcpu->kvm;

  /* create for all vcpus */
  kvm_for_each_vcpu(vcpu_id, vcpu_iter, kvm) {
    /* vmcall optimization first */
    write_nop_to_guest_syscall(vcpu_iter);

    deeplog_log_info("Create deeplog contexts for vcpu_id: %lu.\n", vcpu_id);
    // check id 
    NEVER(vcpu_id != vcpu_iter->vcpu_id);
    for (i = 0; i < DEEPLOG_CTX_MAX; i++) {
        ctx = &deeplog_ctxs[vcpu_id][i];
        ctx_id = i;
        /* create */
        create_deeplog_ctx(ctx, vcpu_iter, ctx_id);
        /* then init contexts based on the profile template */
        init_deeplog_ctx(ctx, vcpu_iter);
    }
  }

  /* set init flag */
  set_deeplog_context_init_flag(1);
}
EXPORT_SYMBOL(vmcall_create_init_deeplog_contexts);

void __destroy_deeplog_ctx(int vcpu_id, int key)
{
    deeplog_ctx_t *deeplog_ctx;
    struct kvm_mmu_page *cur_page, *n;
    struct list_head *leaf_page, *non_leaf_page;
    
    deeplog_ctx = &deeplog_ctxs[vcpu_id][key];
    if (!deeplog_ctx->active)
        return;

    leaf_page = &deeplog_ctx->leaf_page;
    non_leaf_page = &deeplog_ctx->non_leaf_page; 
    
    /* free all existing leaf pages */
    if (!list_empty(leaf_page)) {
        list_for_each_entry_safe(cur_page, n, leaf_page, link) {
            list_del(&cur_page->link);
            kfree(cur_page);
        }
    }
    /* free all non_leaf_pages (should not do actual things) */
    if (!list_empty(non_leaf_page)) {
        list_for_each_entry_safe(cur_page, n, non_leaf_page, link) {
            list_del(&cur_page->link);
            kfree(cur_page);
        }
    }

    /* free all UD2 subpages */

    deeplog_ctx->active = 0;
}

/*
 * we only free contexts at vmx_exit, i.e., unless we use another template
 * to rebuild the hypervisor, we should keep those contexts to avoid re-setup.
 */
void kvm_destroy_deeplog_ctxs(void)
{
    int i, vcpu_id;
    /* make it vcpu aware */
    for (vcpu_id = 0; vcpu_id < VCPU_MAX; vcpu_id++) {
        for (i = 0; i < DEEPLOG_CTX_MAX; i++) {
            __destroy_deeplog_ctx(vcpu_id, i);
        }
    }
    deeplog_log_info("[Done] destroy all deeplog contexts.\n");
}
EXPORT_SYMBOL(kvm_destroy_deeplog_ctxs);

#include "../vmx/vmx.h"

static u64 *vcpu_eptp_list[VCPU_MAX];
u64 *vcpu_eptp_list_fetch(struct kvm_vcpu *vcpu)
{
    if (!vcpu_eptp_list[vcpu->vcpu_id]) {
        vcpu_eptp_list[vcpu->vcpu_id] = (u64 *)phys_to_virt(page_to_phys(to_vmx(vcpu)->eptp_list_pg));
    }
    return vcpu_eptp_list[vcpu->vcpu_id];
}
EXPORT_SYMBOL(vcpu_eptp_list_fetch);

/* 
 * Whenever we enter the syscall for the profiled sensitive application,
 * we activate deeplog context's EPTP list
 */
void vmcall_activate_deeplog_contexts(struct kvm_vcpu *vcpu)
{
    u64 *eptp_list;
    deeplog_ctx_t *ctx;
    int i;
    unsigned long vcpu_id;
    struct kvm_vcpu *vcpu_iter;
    struct kvm *kvm;
    kvm = vcpu->kvm;
    /* make it vcpu aware */
    kvm_for_each_vcpu(vcpu_id, vcpu_iter, kvm) {
        // check cpu id
        NEVER(vcpu_id != vcpu_iter->vcpu_id);
        eptp_list = vcpu_eptp_list_fetch(vcpu_iter);
        deeplog_log_info("[vcpu=%lu] Activate EPTP_LIST: (HVA) 0x%llx.\n",
                          vcpu_id, (u64)eptp_list);
        
        for (i = 0; i < DEEPLOG_CTX_MAX; i++) {
            ctx = &deeplog_ctxs[vcpu_id][i];

            DL_ASSERT(ctx->key == i);

            if (__deeplog_ctx_has_secondary_ept(ctx)) {
                eptp_list[ctx->key] = ctx->eptp;
                deeplog_log_info("[vcpu=%lu,ctx=%d] EPTP_LIST_SLOT[0x%llx] = eptp: 0x%llx",
                                    vcpu_id, ctx->key, (u64)&eptp_list[ctx->key], ctx->eptp);
            } else {
                /* default eptp */
                eptp_list[ctx->key] = eptp_list[511];
            }
        }
    }
}
EXPORT_SYMBOL(vmcall_activate_deeplog_contexts);


/*
 * ctx_id: the syscall_id/VMFUNC_id/EPT_id 
 * gpa: the guest physical address (page-aligned) to be set
 * write_protect: 0 for full-access; 1 for write-protection 
 */
void vmcall_setup_eptp_data_protection(struct kvm_vcpu *vcpu,
                                        int ctx_id,
                                        u64 gpa,
                                        int write_protect)
{
    u64 hpa;
    
    if (!is_deeplog_context_init()) {
        deeplog_log_error("Please initialize deeplog EPTP contexts first.\n");
        deeplog_log_error("First generate template in host OS and rebuild kvm.\n");
        deeplog_log_error("Then execute in guest OS: users/init_eptp_contexts .\n");
        return;
    }

    if (ctx_id < 0 || ctx_id >= DEEPLOG_CTX_MAX) {
        deeplog_log_error("Invalid context id: %d.\n", ctx_id);
        return;
    }

    hpa = gpa_to_hpa(vcpu->kvm, gpa);
    /* TODO: 
     * please check. do we need to make this vcpu aware? 
     * you may want to use kvm_for_each_vcpu() here?
     * Currently doing the dirty patch.
     */
    do_ept_gpa_remap(&deeplog_ctxs[vcpu->vcpu_id][ctx_id], gpa, hpa, 0, write_protect);
    // do_ept_gpa_remap(&deeplog_ctxs[ctx_id], gpa, hpa, 0, write_protect);
}
EXPORT_SYMBOL(vmcall_setup_eptp_data_protection);

// OmniLog (1page) per core buffer without dual-buffer switching
static char percore_buf[VCPU_MAX][4096] __attribute__((aligned(4096)));
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

void vmcall_omnilog_record_syslog(struct kvm_vcpu *vcpu, u64 src_gva, u64 log_length, u64 syscall_id)
{
    struct x86_exception e;
    struct omnisys_log_t *log;
    char *cur_buf = percore_buf[vcpu->vcpu_id];
    // memcpy(cur_buf, (void *)src_gva, log_length);
    kvm_read_guest_virt(vcpu, (gva_t)(src_gva), cur_buf, log_length, &e);
    // printk(KERN_INFO "[vcpu=%d] Syscall: %lld, Log length: %lld.\n", vcpu->vcpu_id, syscall_id, log_length);
    // print the log
    log = (struct omnisys_log_t *)cur_buf;
    printk(KERN_INFO "rdi=%lx, rsi=%lx, rdx=%lx, tsc=%lx.\n",
            log->rdi, log->rsi, log->rdx, log->tsc);
}
EXPORT_SYMBOL(vmcall_omnilog_record_syslog);

/*
 * Pin all guest memory for all VCPUs
 */
int is_deeplog_memory_pinned = 0;
EXPORT_SYMBOL(is_deeplog_memory_pinned);

static void pin_all_memory(struct kvm_vcpu *vcpu) 
{
    struct page *page;
    /* use that pre-defined range shown in guest's boot dmesg */
    gfn_t gfn_start, gfn_end, gfn_iter;
    
    // gfn_start = gpa_to_gfn(0x100000000);
    gfn_start = 0;
    gfn_end = gpa_to_gfn(0x233ffffff);

    /* convert to host hpa page */
    for (gfn_iter = gfn_start; gfn_iter < gfn_end; gfn_iter++) {
        page = kvm_vcpu_gfn_to_page(vcpu, gfn_iter);
        // if (page == KVM_ERR_PTR_BAD_PAGE) {
        //     deeplog_log_error("[vcpu=%d] Failed to pin GFN=0x%llx to a HPA page.\n", 
        //                         vcpu->vcpu_id, gfn_iter);
        //     continue;
        // }
    }
}

void vmcall_pin_all_guest_memory(struct kvm_vcpu *vcpu)
{
    struct kvm *kvm;
    struct kvm_vcpu *vcpu_iter;
    unsigned long vcpu_id;
    kvm = vcpu->kvm;
    
    /* iterate for all VCPUs */
    kvm_for_each_vcpu(vcpu_id, vcpu_iter, kvm) {
        NEVER(vcpu_id != vcpu_iter->vcpu_id);
        pin_all_memory(vcpu_iter);
        deeplog_log_info("[vcpu_id=%ld] Pin guest memory done.\n", vcpu_id);
    }
}
EXPORT_SYMBOL(vmcall_pin_all_guest_memory);

void vmcall_walk_through_all_guest_memory_done(struct kvm_vcpu *vcpu)
{
    /* set up the varible */
    is_deeplog_memory_pinned = 1;
    deeplog_log_info("[vcpu_id=%d] Walk through all guest memory done.\n",
                        vcpu->vcpu_id);
}
EXPORT_SYMBOL(vmcall_walk_through_all_guest_memory_done);

/**********************************************************
 * KVM page-level write-logging
 **********************************************************/

 static int deeplog_ept_setup_gfn_wp(struct kvm_vcpu *vcpu, gfn_t gfn) {
	bool flush = false;
	/* spte success? should flush ept? */
	flush = kvm_vcpu_write_protect_gfn(vcpu, gfn);

	if (flush) {
		kvm_flush_remote_tlbs_with_address(vcpu->kvm, gfn, 1);
		return 1;
	}
	/* 
	 * last-level spte is not set by KVM, we 
     * should not care about this right now.
	 */
	return 0;
}
/**********************************************************
 * Testing VMCALL functions
 **********************************************************/

/* Write protection function */
/*
 * To Spencer:
 * This hypercall is to test KVM page-level write protection (wp).
 * @param gva: the guest virtual page address to be write-logged

 * The corresponding handler for triggered EPT write-protection is at:
 * vmx.c / handle_ept_violation(), line 5918 #ifdef CONFIG_DEEPLOG
 */
char micro_buf[128];       // for microbench write log test
EXPORT_SYMBOL(micro_buf);

int traced_time = 0;
EXPORT_SYMBOL(traced_time);

void vmcall_setup_page_wp(struct kvm_vcpu *vcpu, gva_t gva) {
	gpa_t gpa;
	gfn_t gfn;
	/* translate GVA -> GPA */
	gpa = gva_to_gpa(vcpu, gva, NULL);
	deeplog_log_info("Protect: GPA: 0x%llx <-> GVA: 0x%lx.\n", gpa, gva);
	
    /* sanity check */
    if (!gva || gpa == UNMAPPED_GVA) {
        deeplog_log_error("Requested GPA invalid. Check.\n");
        return;
    }
    
	/* try to use kvm api */
	gfn = gpa_to_gfn(gpa);
	if (deeplog_ept_setup_gfn_wp(vcpu, gfn)) {
		/* success */
        /* this is just for test, set up a global value `traced_gpa` */
        /* You might want to replace it with a global hash map */
        traced_gpa = gpa;
        return;
	}
}
EXPORT_SYMBOL(vmcall_setup_page_wp);


/* set up the second ept */
struct page *host_page;

u64 setup_second_ept(struct kvm_vcpu *vcpu, u64 src_gva, u64 dst_gva) {
    struct kvm_mmu_page *root;
    u64 *eptp_list, eptp;
    u64 ept_root_hpa;
    
    u64 host_page_virt, host_page_phys;

    deeplog_ctx_t second_ept_ctx, third_ept_ctx, fourth_ept_ctx;
    deeplog_log_info("Start to set up the second ept.\n");
    
    eptp_list = (u64 *)phys_to_virt(page_to_phys(to_vmx(vcpu)->eptp_list_pg));

    root = to_shadow_page(vcpu->arch.mmu->root.hpa);
    deeplog_log_info("EPT root hpa: 0x%lx, root->spt: 0x%lx (0x%lx hpa again).\n",
                        (unsigned long)vcpu->arch.mmu->root.hpa,
                        (unsigned long)root->spt,
                        (unsigned long)virt_to_phys(root->spt));
    deeplog_log_info("EPTP list hpa: 0x%lx.\n", (unsigned long)virt_to_phys((void *)eptp_list));

    //
    src_gpa = gva_to_gpa(vcpu, src_gva, NULL);
    dst_gpa = gva_to_gpa(vcpu, dst_gva, NULL);
    dst_hpa = gpa_to_hpa(vcpu->kvm, dst_gpa);
    
    /*=================== start to set up second page table =========================*/
    second_ept_ctx = deeplog_ctxs[0][0];
    third_ept_ctx = deeplog_ctxs[0][1];
    fourth_ept_ctx = deeplog_ctxs[0][2];

    create_deeplog_ctx(&second_ept_ctx, vcpu, 0);
    create_deeplog_ctx(&third_ept_ctx, vcpu, 1);
    create_deeplog_ctx(&fourth_ept_ctx, vcpu, 2);

    write_lock(&vcpu->kvm->mmu_lock);
    /* copy leaf nodes */
    copy_leaf_ept_ptes(&second_ept_ctx.leaf_page, vcpu);
    /* create second ept */
    ept_root_hpa = construct_ept(&second_ept_ctx, 0);

    // third and fourth context
    copy_leaf_ept_ptes(&third_ept_ctx.leaf_page, vcpu);
    construct_ept(&third_ept_ctx, 0);

    copy_leaf_ept_ptes(&fourth_ept_ctx.leaf_page, vcpu);
    construct_ept(&fourth_ept_ctx, 0);

    write_unlock(&vcpu->kvm->mmu_lock);

    /* set up the EPTP vmcs areas */
    eptp = ept_to_eptp((u64)vcpu->arch.mmu->root.hpa, 4);
    eptp_list[0] = eptp;
    eptp_list[1] = eptp;
    eptp_list[2] = eptp;
    eptp_list[3] = second_ept_ctx.eptp;
    eptp_list[4] = third_ept_ctx.eptp;
    eptp_list[5] = fourth_ept_ctx.eptp;
    deeplog_log_info("Configured eptp_list[0]: 0x%llx, eptp_list[1]: 0x%llx, eptp_list[2]: 0x%llx, eptp_list[3]: 0x%llx.\n",
                        eptp_list[0], eptp_list[1], eptp_list[2], eptp_list[3]);
    // we print ept walks
    deeplog_log_info("Start print (default) EPT walk SRC GPA: 0x%llx, EPT hpa: 0x%llx.\n",
                        src_gpa, vcpu->arch.mmu->root.hpa);
    print_ept_walk(vcpu, 0, src_gpa);
    deeplog_log_info("Start print (second) EPT walk SRC GPA: 0x%llx, EPT hpa: 0x%llx.\n",
                        src_gpa, ept_root_hpa);
    print_ept_walk(vcpu, ept_root_hpa, src_gpa);

    deeplog_log_info("Start print (second) EPT walk DST GPA: 0x%llx, EPT hpa: 0x%llx.\n",
                        dst_gpa, ept_root_hpa);
    print_ept_walk(vcpu, ept_root_hpa, dst_gpa);

    /* remap src_gpa -> dst_hpa in second ept */
    do_ept_gpa_remap(&second_ept_ctx, src_gpa, dst_hpa, 0, 0);
    // print walk again
    deeplog_log_info("Start print (second) EPT walk SRC GPA after remap: 0x%llx, EPT hpa: 0x%llx.\n",
                        src_gpa, ept_root_hpa);
    print_ept_walk(vcpu, ept_root_hpa, src_gpa);

    /* remap src_gpa -> a new hpa in the third ept */
    host_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
    host_page_virt = (u64)page_to_virt(host_page);
    host_page_phys = (u64)page_to_phys(host_page);
    memset((void *)host_page_virt, 0x77, PAGE_SIZE);
    deeplog_log_info("Host buf HVA: 0x%llx, HPA: 0x%llx, value: 0x%x.\n", 
                        host_page_virt, host_page_phys, *(int *)host_page_virt);
    
    do_ept_gpa_remap(&third_ept_ctx, src_gpa, host_page_phys, 0, 0);

    // print walk again
    deeplog_log_info("Start print (third) EPT walk SRC GPA after remap: 0x%llx, EPT hpa: 0x%llx.\n",
                        src_gpa, third_ept_ctx.second_ept_ptr_pa);
    print_ept_walk(vcpu, third_ept_ctx.second_ept_ptr_pa, src_gpa);
    return 0;
}
EXPORT_SYMBOL(setup_second_ept);
