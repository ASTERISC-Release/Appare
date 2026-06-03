#include "libpt.h"
#include "metadata.h"
#include "ptmem.h"

#include <linux/wait.h>  
#include <linux/delay.h> 
#include <linux/kernel.h>
#include <linux/llist.h> 

struct page *pt_page;

void *deeplog_pt_buffer;
u64 deeplog_pt_buffer_hpa;

/*
 */
#define PT_PAGE_ORDER	16
#define PT_PAGE_COUNT	(1 << PT_PAGE_ORDER)

// #define PT_ROUNDUP(addr, size)  ( ((addr) + ((size) - 1)) & ~((size) - 1) )
// 2^16 * 4KB = 256MB

/* For each VCPU */
/* We maintain a list of two ToPA buffers */
// static struct page *pt_trace_pages[VCPU_MAX];
// static char *pt_trace_base[VCPU_MAX];    

// #define NR_TOPA_BUFS	2
// static int pt_topa_available[VCPU_MAX][NR_TOPA_BUFS];   // 1: available, 0: is being persisted
// static char *pt_topa_buffers[VCPU_MAX][NR_TOPA_BUFS];

// static int pt_current_buf_id[VCPU_MAX];


# define PER_CORE_COUNT      8
struct topa_buf_pool {
	void *buf;
	struct llist_node node;
};

static struct llist_head topa_buf_pool_list[VCPU_MAX];

static int pool_initalized;

// static spinlock_t pt_topa_lock[VCPU_MAX];
// static wait_queue_head_t buffer_wait_queue[VCPU_MAX];

// static int pt_get_next_buf_id(int vcpu_id) {
// 	return ((pt_current_buf_id[vcpu_id] + 1) % NR_TOPA_BUFS);
// }

/* micro_interrupt cycles */
unsigned long intr_times[VCPU_MAX];
EXPORT_SYMBOL(intr_times);

/* total rdtscp for the NMI handling */
unsigned long total_rdtscp[VCPU_MAX];
EXPORT_SYMBOL(total_rdtscp);

int deeplog_is_pt_init[VCPU_MAX];
EXPORT_SYMBOL(deeplog_is_pt_init);

int deeplog_is_pt_enabled[VCPU_MAX];
EXPORT_SYMBOL(deeplog_is_pt_enabled);

int deeplog_syscall_count[VCPU_MAX];
EXPORT_SYMBOL(deeplog_syscall_count);

static int vmx_init_pt_trace_buffer(int vcpu_id)
{
	int i;
	struct topa_buf_pool *topa_buf;
	
	unsigned int order;
	size_t pages_required, topa_full_size;
	struct page *pages;
	
	struct llist_head *head;
	void *buf;

	topa_full_size = TOPA_BUFFER_SIZE + PAGE_SIZE;
	
	head = &topa_buf_pool_list[vcpu_id];
	init_llist_head(head);

	pages_required = (topa_full_size + PAGE_SIZE - 1) / PAGE_SIZE;
	order = get_order(TOPA_BUFFER_SIZE);

	for (i = 0; i < PER_CORE_COUNT; i++) {
		pages = dma_alloc_from_contiguous(NULL, pages_required, order, false);
		if (!pages) {
			deeplog_log_error("VCPU%d alloc_topa_buf failed.\n", vcpu_id);
			return -1;
		}

		NEVER(!IS_ALIGNED(page_to_phys(pages), TOPA_BUFFER_SIZE));
		
		if (!IS_ALIGNED(page_to_phys(pages), TOPA_BUFFER_SIZE)) {
			deeplog_log_error("VCPU%d alloc_topa_buf failed: 0x%lx is not aligned to order=%d.\n", vcpu_id,
								(unsigned long)page_to_phys(pages), order);
			dma_release_from_contiguous(NULL, pages, pages_required);
			return -1;
		}

		buf = (void *)page_to_virt(pages);
		topa_buf = kmalloc(sizeof(struct topa_buf_pool), GFP_KERNEL);

		if (!topa_buf) {
			deeplog_log_error("VCPU%d alloc_topa_buf failed.\n", vcpu_id);
			dma_release_from_contiguous(NULL, pages, pages_required);
			return -1;
		}

		topa_buf->buf = buf;
		llist_add(&topa_buf->node, head);
		deeplog_log_info("[Done] VCPU%d alloc_topa_buf=0x%lx, nr_pages=%ld.\n", vcpu_id, (unsigned long)page_to_phys(pages), pages_required);
	}
	return 0;
}

static void debug_traverse_topa_list(int vcpu_id) {
	struct llist_node *node;
	struct topa_buf_pool *topa_buf;
	struct llist_head *head;

	head = &topa_buf_pool_list[vcpu_id];
	llist_for_each(node, head->first) {
		topa_buf = llist_entry(node, struct topa_buf_pool, node);
		deeplog_log_info("VCPU%d traversed topa_buf=0x%lx.\n", vcpu_id, (unsigned long)virt_to_phys(topa_buf->buf));
	}
}

static void *pt_get_available_topa_buf(int vcpu_id)
{
	struct llist_node *node;
	struct topa_buf_pool *topa_buf;

	if (!pool_initalized) {
		deeplog_log_error("VCPU%d topa_buf_pool_list not initialized.\n", vcpu_id);
		return NULL;
	}

	node = llist_del_first(&topa_buf_pool_list[vcpu_id]);
	if (!node) {
		deeplog_log_error("VCPU%d topa_buf_pool_list is used up.\n", vcpu_id);
		return NULL;
	}

	topa_buf = llist_entry(node, struct topa_buf_pool, node);
	return topa_buf->buf;
}

static void pt_release_topa_buf(int vcpu_id, void *raw)
{
	struct topa_buf_pool *topa_buf;

	if (!pool_initalized) {
		deeplog_log_error("VCPU%d topa_buf_pool_list not initialized.\n", vcpu_id);
		return;
	}

	topa_buf = kmalloc(sizeof(struct topa_buf_pool), GFP_ATOMIC);
	if (!topa_buf) {
		deeplog_log_error("VCPU%d topa_buf struct node alloc failed.\n", vcpu_id);
		return;
	}

	topa_buf->buf = raw;
	llist_add(&topa_buf->node, &topa_buf_pool_list[vcpu_id]);
}


// // static void *buf[VCPU_MAX];
// static void test_init_destroy(int vcpuid, int init) {
// 	if (init) { 
// 		// buf[vcpuid] = pt_get_available_topa_buf(vcpuid);
// 		// if (!buf[vcpuid]) {
// 		// 	deeplog_log_error("pt_get_available_topa_buf failed.\n");
// 		// 	return;
// 		// }
// 		// deeplog_log_info("pt_get_available_topa_buf success buf=0x%lx.\n", (unsigned long)(buf[vcpuid]));

// 		spin_lock_init(&pt_topa_lock[vcpuid]);
// 	} else {
// 		// pt_release_topa_buf(vcpuid, buf[vcpuid]);
// 		// deeplog_log_info("pt_release_topa_buf success.\n");
// 	}
// }

static void *vmx_destroy_pt_trace_buffer(int vcpu_id)
{
    struct llist_node *node;
	struct topa_buf_pool *topa_buf;

	size_t topa_full_size, nr_pages;

	topa_full_size = TOPA_BUFFER_SIZE + PAGE_SIZE;
	nr_pages = (topa_full_size + PAGE_SIZE - 1) / PAGE_SIZE;
	
	while ((node = llist_del_first(&topa_buf_pool_list[vcpu_id]))) {
		topa_buf = llist_entry(node, struct topa_buf_pool, node);
		dma_release_from_contiguous(NULL, virt_to_page(topa_buf->buf), nr_pages);
		deeplog_log_info("VCPU%d destroy topa_buf=0x%lx.\n", vcpu_id, (unsigned long)virt_to_phys(topa_buf->buf));
		kfree(topa_buf);
	}
    
	return NULL;
}


static struct kmem_cache *pt_buffer_cache = NULL;
static struct kmem_cache *pt_image_cache = NULL;

static struct workqueue_struct *pt_wq;

static atomic64_t pt_flying_tasks = ATOMIC_INIT(0);

/**
 * Common
 */
u64 deeplog_pt_topa_base[VCPU_MAX];
u64 deeplog_pt_topa_mask[VCPU_MAX];

static u64 pt_topa_base(struct kvm_vcpu *vcpu)
{
	int ret, vcpu_id;
	vcpu_id = vcpu->vcpu_id;
	ret = kvm_get_msr(vcpu, MSR_IA32_RTIT_OUTPUT_BASE, &deeplog_pt_topa_base[vcpu_id]);
	if (ret == 1 || ret == KVM_MSR_RET_INVALID) {
		deeplog_log_error("kvm(vmx)_get_msr(MSR_IA32_RTIT_OUTPUT_BASE) failed.\n");
		NEVER(ret == 1 || ret == KVM_MSR_RET_INVALID);
		// return 0;
	}
	return deeplog_pt_topa_base[vcpu_id];
}

/*
 * `proc_trace_output_offset` (current region write offset): 
 * This a pointer into the current output region and indicates the location of the next write.
 */
static u64 pt_topa_offset(struct kvm_vcpu *vcpu)
{
	int ret, vcpu_id;
	vcpu_id = vcpu->vcpu_id;
	ret = kvm_get_msr(vcpu, MSR_IA32_RTIT_OUTPUT_MASK, &deeplog_pt_topa_mask[vcpu_id]);
	if (ret == 1 || ret == KVM_MSR_RET_INVALID) {
		deeplog_log_error("kvm(vmx)_get_msr(MSR_IA32_RTIT_OUTPUT_MASK) failed.\n");
		NEVER(ret == 1 || ret == KVM_MSR_RET_INVALID);
		// return 0;
	}
	return deeplog_pt_topa_mask[vcpu_id] >> 32;
}

/*
 * `proc_trace_table_offset` (table index): 
 * This indicates the entry of the current table that is currently in use.
 */
static u64 pt_topa_index(struct kvm_vcpu *vcpu)
{
	int ret, vcpu_id;
	vcpu_id = vcpu->vcpu_id;
	ret = kvm_get_msr(vcpu, MSR_IA32_RTIT_OUTPUT_MASK, &deeplog_pt_topa_mask[vcpu_id]);
	if (ret == 1 || ret == KVM_MSR_RET_INVALID) {
		deeplog_log_error("kvm(vmx)_get_msr(MSR_IA32_RTIT_OUTPUT_MASK) failed.\n");
		NEVER(ret == 1 || ret == KVM_MSR_RET_INVALID);
		// return 0;
	}
	return ((deeplog_pt_topa_mask[vcpu_id] & 0xffffffff) >> 7);
}

static int pt_enabled(struct kvm_vcpu *vcpu)
{
	u64 data;
	int ret;
	ret = kvm_get_msr(vcpu, MSR_IA32_RTIT_CTL, &data);
	if (ret == 1 || ret == KVM_MSR_RET_INVALID) {
		deeplog_log_error("kvm(vmx)_get_msr(MSR_IA32_RTIT_CTL) failed.\n");
		NEVER(ret == 1 || ret == KVM_MSR_RET_INVALID);
		// return 0;
	}
	return data & RTIT_CTL_TRACEEN;
}

/**
 * PT log file, make multiple cores
 */
static struct file *pt_logfile[VCPU_MAX];
static loff_t pt_logfile_off[VCPU_MAX];
static struct mutex pt_logfile_mtx[VCPU_MAX];
// static DEFINE_MUTEX(pt_logfile_mtx);

/* store trace in pt.log.x file */
#define pt_close_logfile(vcpu_id) do { \
	if (pt_logfile[vcpu_id]) { \
		filp_close(pt_logfile[vcpu_id], NULL); \
		pt_logfile[vcpu_id] = NULL; \
		pt_logfile_off[vcpu_id] = 0; \
	} \
} while (0)

#define pt_log_file(vcpu_id, buf, count) do { \
	ssize_t s; \
	NEVER(!pt_logfile[vcpu_id]); \
	s = kernel_write(pt_logfile[vcpu_id], (char *) buf, count, &pt_logfile_off[vcpu_id]); \
	UNHANDLED(s < 0); \
	pt_logfile_off[vcpu_id] += s; \
} while (0)

/**
 * PT memory (useless)
 */

/* store trace in pt_buffer memory */
static struct dentry *pt_memory_dentry = NULL;

static char *pt_memory = NULL;
static loff_t pt_memory_off = 0;
static DEFINE_MUTEX(pt_memory_mtx);

#define pt_close_memory() do { \
	if (pt_memory) { \
		vfree(pt_memory); \
		pt_memory = NULL; \
		pt_memory_off = 0; \
	} \
} while (0)

/**
 * PT log file log content 
 */

#pragma pack(push)
struct pt_logfile_header {
	u32 magic;
	u32 version;
};

/* defined by Griffin, no particular use */
#define PT_LOGFILE_MAGIC 0x51C0FFEE
#define PT_LOGFILE_VERSION 0x1

/* write intel pt metadata */
static void pt_log_header(int vcpu_id)
{
	struct pt_logfile_header h = {
		.magic = PT_LOGFILE_MAGIC,
		.version = PT_LOGFILE_VERSION,
	};

	mutex_lock(&pt_logfile_mtx[vcpu_id]);
	pt_log_file(vcpu_id, &h, sizeof(h));
	mutex_unlock(&pt_logfile_mtx[vcpu_id]);
}

enum pt_logitem_kind {
	PT_LOGITEM_BUFFER,
	PT_LOGITEM_PROCESS,
	PT_LOGITEM_THREAD,
	PT_LOGITEM_IMAGE,
	PT_LOGITEM_XPAGE,
	PT_LOGITEM_UNMAP,
	PT_LOGITEM_FORK,
	PT_LOGITEM_SECTION,
	PT_LOGITEM_THREAD_END,
	PT_LOGITEM_AUDIT,
};

struct pt_logitem_header {
	enum pt_logitem_kind kind;
	u32 size;
};

struct pt_logitem_buffer {
	struct pt_logitem_header header;
	u64 tgid;
	u64 pid;
	u64 sequence;
	u64 size;
};

/* write intel pt trace data */
static void pt_log_buffer(struct pt_buffer *buf)
{
	int vcpu_id;
	struct pt_logitem_buffer item = {
		.header = {
			.kind = PT_LOGITEM_BUFFER,
			.size = sizeof(struct pt_logitem_buffer) + buf->size,
		},
		.tgid = 0,
		.pid = 0,
		.sequence = buf->sequence,
		.size = buf->size,
	};
	
	vcpu_id = buf->vcpu->vcpu_id;

	mutex_lock(&pt_logfile_mtx[vcpu_id]);
	pt_log_file(vcpu_id, &item, sizeof(item));
	pt_log_file(vcpu_id, buf->raw, buf->size);
	mutex_unlock(&pt_logfile_mtx[vcpu_id]);
}
#pragma pack(pop)


/** 
 * ====================================================
 *  PT Library functions (useless rn)
 * ====================================================
 */

static ssize_t pt_memory_read(struct file *file, char __user *user_buf,
                              size_t count, loff_t *offset) {
    return simple_read_from_buffer(user_buf, count, offset, 
                                   pt_memory, pt_memory_off);
}

static ssize_t pt_buffer_write(struct file *file, const char __user *user_buf,
                               size_t count, loff_t *offset) {
    return simple_write_to_buffer(pt_memory, MEMORY_SIZE - 1, offset,
                                  user_buf, count);
}

static const struct file_operations pt_memory_fops = {
    .read = pt_memory_read,
    .write = pt_buffer_write,
};

static int pt_memory_setup(void) 
{
    /* create a file in the debugfs filesystem */
    pt_memory_dentry = debugfs_create_file("pt_memory",0600, NULL, NULL, 
                                        &pt_memory_fops);
    if (!pt_memory_dentry)
        return -ENOMEM;

    return 0;
}

static void pt_memory_destroy(void) {
	if (pt_memory_dentry)
        debugfs_remove(pt_memory_dentry);
}


static int pt_wq_setup(void)
{
	int err = -ENOMEM;

	pt_wq = alloc_workqueue("pt_wq", WQ_UNBOUND | WQ_HIGHPRI, 1);
	if (!pt_wq)
		goto fail;

	return 0;

fail:
	return err;
}

static void pt_wq_destroy(void)
{
	if (pt_wq){
		flush_workqueue(pt_wq);
		destroy_workqueue(pt_wq);
	}
}

static int pt_monitor_create(void)
{
	int i;
	char *fp;
    if (atomic64_read(&pt_flying_tasks) > 0) {
        /* PT is in using for tracing */
        deeplog_log_error("PT is flying for tasks.\n");
        return -EBUSY;
    }

    /* init PT logfile */
	for (i = 0; i < VCPU_MAX; i++) {
		pt_close_logfile(i);

		mutex_init(&pt_logfile_mtx[i]);
		fp = kasprintf(GFP_KERNEL, "/var/log/pt.log.%d", i);
		pt_logfile[i] = filp_open(fp, O_WRONLY | O_TRUNC 
                    | O_CREAT | O_LARGEFILE, 0644);   
		kfree(fp);
    	if (IS_ERR_OR_NULL(pt_logfile[i]))
			return PTR_ERR(pt_logfile[i]);
	}

    /* init PT memory (useless) */
    pt_close_memory();
    pt_memory = (char *) vmalloc(MEMORY_SIZE);
    if (!pt_memory)
        return -ENOMEM;
    memset(pt_memory, 0xff, MEMORY_SIZE);

	/* log all headers */
	for (i = 0; i < VCPU_MAX; i++){
		pt_log_header(i);
	}
    
	workqueue_set_max_active(pt_wq, 1);

	deeplog_log_info("PT monitor task created.\n");
	return 0;
}

/*
 * ToPA buffer initialization and assignment
 */
static void do_setup_topa(struct topa *topa, void *raw)
{
	/* checking virtual address is fine given 1:1 direct mapping */
    #define DIRECT_MAPPING_END 0xffffc7ffffffffff
	NEVER((unsigned long) topa > DIRECT_MAPPING_END);
	NEVER((unsigned long) raw > DIRECT_MAPPING_END);
	NEVER((unsigned long) raw & (TOPA_BUFFER_SIZE - 1));

	/* setup topa entries */
	topa->entries[0] = TOPA_ENTRY(virt_to_phys(raw),
			TOPA_ENTRY_SIZE_CHOICE, 0, 1, 0);
	topa->entries[1] = TOPA_ENTRY(virt_to_phys(raw + TOPA_BUFFER_SIZE),
			TOPA_ENTRY_SIZE_4K, 0, 1, 0);
	topa->entries[2] = TOPA_ENTRY(virt_to_phys(topa), 0, 0, 0, 1);

	topa->raw = raw;
}

static void pt_setup_topa(struct topa *topa, void *raw)
{
	topa->sequence = 0;
	topa->n_processed = 0;
	INIT_LIST_HEAD(&topa->buffer_list);
	spin_lock_init(&topa->buffer_list_sl);
	topa->failed = false;
	topa->index = 0;
	
	do_setup_topa(topa, raw);
}

static struct topa *pt_alloc_topa(struct kvm_vcpu *vcpu)
{
	struct topa *topa;
	void *raw;

	topa = (struct topa *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 1);
	if (!topa)
		goto fail;
	
	// raw = (void *) kmem_cache_alloc(pt_trace_cache, GFP_KERNEL);
	raw = pt_get_available_topa_buf(vcpu->vcpu_id);
	if (!raw)
		goto free_topa;
	
	pt_setup_topa(topa, raw);
	return topa;

free_topa:
	free_pages((unsigned long) topa, 1);
fail:
	return NULL;
}

static void pt_work(struct work_struct *work)
{
	int vcpu_id;
	struct pt_buffer *buf = (struct pt_buffer *) work;
	vcpu_id = buf->vcpu->vcpu_id;
	/* log the buffer into file */
	pt_log_buffer(buf);
	/* free the buffer */
	
	/* release the persisted buffer as available */
	pt_release_topa_buf(vcpu_id, buf->raw);
	/* debug: release kmem cache */
	kmem_cache_free(pt_buffer_cache, buf);
}

static void pt_tasklet(unsigned long data)
{
	struct pt_buffer *buf = (struct pt_buffer *) data;
	queue_work(pt_wq, &buf->work);
}

/* assign the trace transfer task to a worker */
static int pt_move_trace_to_work(struct kvm_vcpu *vcpu, struct topa *topa, u32 size, 
								struct topa *child_topa, bool waiting)
{
	struct pt_buffer *buf;
	buf = kmem_cache_alloc(pt_buffer_cache, GFP_ATOMIC);
	if (!buf)
		goto fail;
	
	INIT_WORK(&buf->work, pt_work);
	tasklet_init(&buf->tasklet, pt_tasklet, (unsigned long) buf);
	INIT_LIST_HEAD(&buf->entry);
	buf->vcpu = vcpu;
	buf->topa = topa;
	buf->child_topa = child_topa;
	buf->size = size;
	buf->index = 0;
	buf->raw = topa->raw;
	
	buf->sequence = topa->sequence;

	/* programming tasklets for running is called scheduling. */
	tasklet_schedule(&buf->tasklet);
	return 0;

fail:
	return -ENOMEM;
}

static void pt_flush_trace(struct kvm_vcpu *vcpu, struct topa *child_topa, bool waiting)
{
	u32 size;
	struct topa *topa;
	void *new_buffer;

	NEVER(pt_enabled(vcpu));

	topa = phys_to_virt(pt_topa_base(vcpu));
	if (topa->failed && !child_topa && !waiting)
		goto end;
	
	size = pt_topa_offset(vcpu) + (pt_topa_index(vcpu)? TOPA_BUFFER_SIZE: 0);
	
	/* get the new buffer for the following PT tracing */
	// spin_lock_irq(&pt_topa_lock[vcpu->vcpu_id]);     // bad code to use lock in NMI
	new_buffer = pt_get_available_topa_buf(vcpu->vcpu_id);
	// spin_unlock_irq(&pt_topa_lock[vcpu->vcpu_id]);
	if (!new_buffer)
		goto failed;

	/* persist the current buffer */
	if (pt_move_trace_to_work(vcpu, topa, size, child_topa, waiting) < 0)
		goto free_new_buffer;
	
	/* assign new buffer for subsequent tracing */
	do_setup_topa(topa, new_buffer);
end:
	/* clear status and mask */
	return;

free_new_buffer:
	// kmem_cache_free(pt_trace_cache, new_buffer);
	pt_release_topa_buf(vcpu->vcpu_id, new_buffer);
failed:
	UNHANDLED(child_topa || waiting);
	// pt_fail_topa(topa, "out of memory");
	goto end;
}

/*
 * PT msr configuration
 */
static void pt_resume(struct kvm_vcpu *vcpu)
{
	int ret;
	u64 data;
	/* read RTIT_CTL */
	if ((ret = kvm_get_msr(vcpu, MSR_IA32_RTIT_CTL, &data)) == 1) {
		deeplog_log_error("kvm(vmx)_get_msr(MSR_IA32_RTIT_CTL) failed.\n");
	}
	/* we assume it has been set correctly */
	NEVER(!(data & RTIT_CTL_OS));
	NEVER(!(data & RTIT_CTL_BRANCH_EN));
	NEVER(!(data & RTIT_CTL_TOPA));
	NEVER(!(data & ((PSBFREQ_SIZE_CHOICE + 1) << 24)));

	/* it cannot be already started */
	NEVER((data & RTIT_CTL_TRACEEN));
	
	/* resume PT */
	data |= RTIT_CTL_TRACEEN;
	if ((ret = kvm_set_msr(vcpu, MSR_IA32_RTIT_CTL, data)) == 1) {
		deeplog_log_error("kvm(vmx)_set_msr(MSR_IA32_RTIT_CTL) failed.\n");
	}
}

static void pt_pause(struct kvm_vcpu *vcpu)
{
	int ret;
	u64 data;
	
	/* read RTIT_CTL */
	if ((ret = kvm_get_msr(vcpu, MSR_IA32_RTIT_CTL, &data)) == 1) {
		deeplog_log_error("kvm(vmx)_get_msr(MSR_IA32_RTIT_CTL) failed.\n");
	}
	
	/* we assume it has been set correctly */
	NEVER(!(data & RTIT_CTL_OS));
	NEVER(!(data & RTIT_CTL_BRANCH_EN));
	NEVER(!(data & RTIT_CTL_TOPA));
	// NEVER(!(data & RTIT_CTL_DISRETC));
	NEVER(!(data & ((PSBFREQ_SIZE_CHOICE + 1) << 24)));

	/* pause PT */
	data &= ~RTIT_CTL_TRACEEN;
	if ((ret = kvm_set_msr(vcpu, MSR_IA32_RTIT_CTL, data)) == 1) {
		deeplog_log_error("kvm(vmx)_set_msr(MSR_IA32_RTIT_CTL) failed.\n");
	}
}

/* Initialize (but not enable) PT tracing msr */
static void pt_setup_msr(struct kvm_vcpu *vcpu, struct topa *topa)
{
	int ret;
    u64 data;
	NEVER(pt_enabled(vcpu));

	/* setup msr */
	/* clear status */
	if ((ret = kvm_set_msr(vcpu, MSR_IA32_RTIT_STATUS, 0)) == 1) {
        deeplog_log_error("kvm(vmx)_set_msr(MSR_IA32_RTIT_STATUS) failed.\n");
    }

    /* configure ToPA base */
    if ((ret = kvm_set_msr(vcpu, MSR_IA32_RTIT_OUTPUT_BASE, virt_to_phys(topa))) == 1) {
        deeplog_log_error("kvm(vmx)_set_msr(MSR_IA32_RTIT_OUTPUT_BASE) failed.\n");
    }
	
	/* clear OUTPUT base mask */
	if ((ret = kvm_set_msr(vcpu, MSR_IA32_RTIT_OUTPUT_MASK, 0)) == 1) {
		deeplog_log_error("kvm(vmx)_set_msr(MSR_IA32_RTIT_OUTPUT_MASK) failed.\n");
	}

	/* setup PT ctl and enable PT */
    data =  RTIT_CTL_OS | RTIT_CTL_BRANCH_EN | 
           RTIT_CTL_TOPA | ((PSBFREQ_SIZE_CHOICE + 1) << 24);

	/* disable RETC */
	// data |= RTIT_CTL_DISRETC;

	/* CYC mode */
	// data |= RTIT_CTL_CYCLEACC;

	/* dont enable PT */
	data &= ~RTIT_CTL_TRACEEN;

    if ((ret = kvm_set_msr(vcpu, MSR_IA32_RTIT_CTL, data)) == 1) {
        deeplog_log_error("kvm(vmx)_set_msr(MSR_IA32_RTIT_CTL) failed.\n");
    }
}

/* Initialize PT */
static inline struct topa *pt_attach(struct kvm_vcpu *vcpu)
{
	struct topa *topa = pt_alloc_topa(vcpu);
	UNHANDLED(!topa);

	/* initialize PT */
	pt_setup_msr(vcpu, topa);

	/* lock */
	atomic64_inc(&pt_flying_tasks);

	return topa;
}

/* Detach PT */
static inline void pt_detach(struct kvm_vcpu *vcpu)
{
	struct topa *topa;
	u64 pt_buf_size;
	// NEVER(!pt_enabled(vcpu));
	pt_pause(vcpu);

	topa = phys_to_virt(pt_topa_base(vcpu));

	/* forcely dump all current trace before detach */
	pt_buf_size = pt_topa_offset(vcpu);

	deeplog_log_info("[vcpu=%d] Got topa page: 0x%llx. Buffer size: 0x%llx.\n", 
						vcpu->vcpu_id, (u64)topa, pt_buf_size);
	pt_move_trace_to_work(vcpu, topa, pt_buf_size, NULL, true);

	/* free ToPA page */
	free_pages((unsigned long) topa, 1);

	atomic64_dec(&pt_flying_tasks);
}

void pt_on_interrupt(struct kvm_vcpu *vcpu)
{
	unsigned long start, end;
	int pt_on;
	start = dl_rdtscp();  // for cycle

	pt_on = pt_enabled(vcpu);
	/* pause tracing */
	if (pt_on)
		pt_pause(vcpu);
	
	/* flush trace */
	// NEVER(pt_topa_index(vcpu) == 0);
	pt_flush_trace(vcpu, NULL, false);
	
	/* DO nothing for debugging now */
	// deeplog_log_info("PT interrupt Triggered.\n");

	/* resume tracing */
	if (pt_on)
		pt_resume(vcpu);
	
	end = dl_rdtscp();  // for cycle
	intr_times[vcpu->vcpu_id]++;
	total_rdtscp[vcpu->vcpu_id] += (end - start);
}
EXPORT_SYMBOL(pt_on_interrupt);

bool is_buffer_zero(const char *buf, size_t size)
{
	size_t i;
	for (i = 0; i < size; i++) {
		if (buf[i] != 0)
			return false;
	}
	return true;
}

bool are_buffers_diff(const char *buf1, const char *buf2, size_t size)
{
	size_t i;
	for (i = 0; i < size; i++) {
		if (buf1[i] != buf2[i])
			return true;
	}
	return false;

}

void pt_for_dump(struct kvm_vcpu *vcpu)
{
	int i;
	ssize_t s, r, length, to_read_sz, already_read_sz;
	static struct file *vmem_file = NULL;
	static loff_t vmemfile_off = 0;
	
	u64 gva, gpa;
	char *buf, *temp_buf;

	for (i = 0; i < num_vmems; i++) {
		/* open vmem file */
		vmem_file = filp_open(GET_VMEM_PATH(i), O_RDWR, 0);
		if(IS_ERR(vmem_file)) {
			deeplog_log_error("open vmem file failed.\n");
			return;
		} else {
			deeplog_log_info("start read vmem file %s.\n", GET_VMEM_PATH(i));
		}

		/* read file to the buf */
		length = (ssize_t)GET_VMEM_LEN(i);
		buf = (char *)vmalloc(length);
		if (!buf) {
			deeplog_log_error("vmalloc buf (length: 0x%lx) failed.\n",
								length);
			filp_close(vmem_file, NULL);
			return;
		}
		vmemfile_off = 0;
		s = kernel_read(vmem_file, buf, length, &vmemfile_off);
		if(s != length) {
			deeplog_log_info("[read vmem file %s] expected length: 0x%lx, actual length: 0x%lx.\n",
								GET_VMEM_PATH(i), length, s);
		}

		/* overwrite file based on dynamic guest kernel memory pages */
		already_read_sz = 0;
		to_read_sz = 0;
		while (already_read_sz != length) {
			to_read_sz = (length - already_read_sz >= PAGE_SIZE) ? 
							PAGE_SIZE: 
							(length - already_read_sz);

			temp_buf = (char *)kmalloc(to_read_sz, GFP_KERNEL | __GFP_ZERO);
			if (!temp_buf) {
				deeplog_log_error("kmalloc temp_buf failed.\n");
				filp_close(vmem_file, NULL);
				return;
			}
			gva = GET_VMEM_START(i) + already_read_sz;
			gpa = kvm_mmu_gva_to_gpa_system(vcpu, gva, NULL);
			// debug
			// deeplog_log_info("gva: %llx <--> gpa: %llx.\n", gva, gpa);
			r = kvm_vcpu_read_guest(vcpu, gpa, (void *)temp_buf, to_read_sz);
			if (r < 0) {
				deeplog_log_error("kvm_vcpu_read_guest read page GVA: 0x%llx, GPA: 0x%llx failed.\n",
									gva, gpa);
				// BUG();
				/* failed to read this page? we skip */
				goto next;
			}
			/* page swapped? then we skip this page. */
			if (is_buffer_zero(temp_buf, to_read_sz)) {
				goto next;
			}
			/* overwrite the original vmlinux static file region */
			if (are_buffers_diff((const char *)(buf + already_read_sz), 
								 (const char *)temp_buf, to_read_sz))
			{
				deeplog_log_info("Diff detected at kernel VA: 0x%llx.\n", gva);
				memcpy(buf + already_read_sz, temp_buf, to_read_sz);
			}
next:
			already_read_sz += to_read_sz;
			kfree(temp_buf);
		}

		/* overwrite the buf back to the file */
		vmemfile_off = 0;
		s = kernel_write(vmem_file, buf, length, &vmemfile_off);
		UNHANDLED(s != length);
		
		/* done. close it */
		vfree(buf);
		filp_close(vmem_file, NULL);
	}
}


/*
 * DeepLog PT module initialization.
 */
int vmx_init_pt(void) 
{
    int ret = -ENOMEM;
    int i;
    /* create a cache for buffers to enable dynamic (de)allocation */
	pt_buffer_cache = kmem_cache_create("pt_buffer_cache",
			sizeof(struct pt_buffer), 0, 0, NULL);
	if (!pt_buffer_cache) {
		deeplog_log_error("kmem_cache_create(pt_buffer_cache) failed.\n");
		goto fail;
	}

	/* memory buffers */
	for (i = 0; i < VCPU_MAX; i++) {
		vmx_init_pt_trace_buffer(i);
	}
	pool_initalized = 1;

	// debug
	debug_traverse_topa_list(0);

	// for (i = 0; i < VCPU_MAX; i++) {
	// 	test_init_destroy(i, 1);
	// }
	
	/* create a cache for dentry filepaths */
	pt_image_cache = kmem_cache_create("pt_image_cache",
			PATH_MAX, PATH_MAX, 0, NULL);
	if (!pt_image_cache) {
		deeplog_log_error("kmem_cache_create(pt_image_cache) failed.\n");
		goto destroy_trace_cache;
	}

    /* setup the workqueue for async computation */
	ret = pt_wq_setup();
	if (ret < 0) {
		deeplog_log_error("pt_wq_setup failed.\n");
		goto destroy_image_cache;
	}
	// /* create pt_monitor file */
    ret = pt_monitor_create();
    if (ret < 0) {
		deeplog_log_error("pt_monitor_create failed.\n");
        goto destroy_pt_memory;
    }

	/* create pt_memory (shared memory) */
	ret = pt_memory_setup();
	if (ret < 0) {
		deeplog_log_error("pt_memory_setup failed.\n");
		goto destroy_pt_memory;
    }
    deeplog_log_info("PT module initialized correctly.\n");
	return ret;

destroy_pt_memory:
	pt_memory_destroy();
// destroy_wq:
	pt_wq_destroy();
destroy_image_cache:
	kmem_cache_destroy(pt_image_cache);
destroy_trace_cache: 
	// kmem_cache_destroy(pt_trace_cache); // deled

	for (i = 0; i < VCPU_MAX; i++)
		vmx_destroy_pt_trace_buffer(i);
	// for (i = 0; i < VCPU_MAX; i++)
	// 	test_init_destroy(i, 0);
	pool_initalized = 0;

// destroy_buffer_cache:
	kmem_cache_destroy(pt_buffer_cache);
	
fail:
    deeplog_log_error("PT module initialized failed.\n");
	return ret;
}
EXPORT_SYMBOL(vmx_init_pt);

/* Exit VMX */
void vmx_exit_pt(void)
{
    // NEVER(pt_enabled());
	int i = 0;
	/* 
	 * In principle, we should restore to host's saved PT configuration.
	 * Here we simply disable PT.
	 */
	wrmsrl(MSR_IA32_RTIT_CTL, 0);

	for (i = 0; i < VCPU_MAX; i++)
		deeplog_is_pt_init[i] = 0;

	pt_close_memory();
	
	for (i = 0; i < VCPU_MAX; i++)
		pt_close_logfile(i);
	
	pt_memory_destroy();
	// pt_monitor_destroy();
	pt_wq_destroy();
	kmem_cache_destroy(pt_image_cache);
	// kmem_cache_destroy(pt_trace_cache);

	for (i = 0; i < VCPU_MAX; i++)
		vmx_destroy_pt_trace_buffer(i);

	kmem_cache_destroy(pt_buffer_cache);
    deeplog_log_info("PT module exited.\n");
}
EXPORT_SYMBOL(vmx_exit_pt);

/**
 * @reminder: All PT control registers (except IA32_RTIT_CTL)
 * should be set before setting IA32_RTIT_CTL.
 * KVM should pass pt_can_write_msr(), see vmx.c.
 */

/* 
 * TEST: initialize and enable PT.
 * In guest VM, use hypercall(rax = 0x20005) to invoke this function.
 */
void deeplog_pt_init(struct kvm_vcpu *vcpu){
	// struct vcpu_vmx *vmx = to_vmx(vcpu);
	/* Let's attach (initialize) PT! */
	pt_attach(vcpu);

	/* enable PT */
	pt_resume(vcpu);
    // deeplog_log_info("pt_desc.guest.status: 0x%llx.\n", vmx->pt_desc.guest.status);
}
EXPORT_SYMBOL(deeplog_pt_init);

/* 
 * TEST: start PT tracing (not used).
 * In guest VM, use hypercall(rax = 0x20006) to invoke this function.
 */
void deeplog_pt_start(struct kvm_vcpu *vcpu){
    pt_resume(vcpu);
}
EXPORT_SYMBOL(deeplog_pt_start);

void deeplog_pt_init_start(struct kvm_vcpu *vcpu, int syscall_id)
{
	if (!deeplog_is_pt_init[vcpu->vcpu_id]) { /* init and enable PT */
			deeplog_is_pt_init[vcpu->vcpu_id] = 1;
			deeplog_is_pt_enabled[vcpu->vcpu_id] = 1;
			deeplog_pt_init(vcpu); // init and start (enable)
		}
	else if (!deeplog_is_pt_enabled[vcpu->vcpu_id]) { /* just start (resume) PT */
				deeplog_is_pt_enabled[vcpu->vcpu_id] = 1;
				deeplog_pt_start(vcpu); // start (resume)
	}

	printk(KERN_INFO "VCPU%d: PT started on syscall=%d.\n", vcpu->vcpu_id, syscall_id);
}
EXPORT_SYMBOL(deeplog_pt_init_start);

/* 
 * TEST: stop PT tracing (not used).
 * In guest VM, use hypercall(rax = 0x20007) to invoke this function.
 */
void deeplog_pt_stop(struct kvm_vcpu *vcpu) {
    pt_pause(vcpu);
}
EXPORT_SYMBOL(deeplog_pt_stop);


void vmcall_sysend_pt_stop(struct kvm_vcpu *vcpu) {
	if (deeplog_is_pt_enabled[vcpu->vcpu_id]) {
		pt_pause(vcpu);
		deeplog_is_pt_enabled[vcpu->vcpu_id] = 0;
	}
	deeplog_syscall_count[vcpu->vcpu_id]++;
}
EXPORT_SYMBOL(vmcall_sysend_pt_stop);

/* 
 * destroy PT configurations.
 * In guest VM, use hypercall(rax = 0x20008) to invoke this function.
 */
void deeplog_pt_destroy(struct kvm_vcpu *vcpu){
	// struct vcpu_vmx *vmx = to_vmx(vcpu);
	// deeplog_log_info("pt_desc.guest.status: %llx.\n", vmx->pt_desc.guest.status);
	if (pt_enabled(vcpu))
		pt_detach(vcpu);
}
EXPORT_SYMBOL(deeplog_pt_destroy);

void deeplog_pt_vmeminfo(struct kvm_vcpu *vcpu)
{
	int i;
	// char fname[] = "/home/chuqi/GitHub/deeplog-kern/host-os/outputs/vmlinux_code.metadata";
	read_vmem_ranges(VMLINUX_METADATA_PATH);	

	deeplog_log_info("PT VMLinux VMEM metadata.\n");
	deeplog_log_info("number of vmems: %d.\n", num_vmems);
	for (i = 0; i < num_vmems; i++) {
		deeplog_log_info("vmem[%d] start=0x%lx, end=0x%lx, length=0x%lx.\n",
							i, vmem_ranges[i].start, 
							vmem_ranges[i].end, vmem_ranges[i].length);
	}
}
EXPORT_SYMBOL(deeplog_pt_vmeminfo);

/*
 * Dump memory based on vmlinux metadata information
 */
void deeplog_pt_memdump(struct kvm_vcpu *vcpu)
{
	int i;
	deeplog_log_info("PT memory informations.\n");
	/* read vmem ranges first */
	read_vmem_ranges(VMLINUX_METADATA_PATH);
	for (i = 0; i < NR_LINUX_CODESECTIONS; i++) {
		deeplog_log_info("VMEM_PATH%d: %s.\n", i, GET_VMEM_PATH(i));
		deeplog_log_info("VMEM_START%d: %lx.\n", i, GET_VMEM_START(i));
		deeplog_log_info("VMEM_LEN%d: %lx.\n", i, GET_VMEM_LEN(i));
	}
	/* let's try dump file. */
	pt_for_dump(vcpu);
}
EXPORT_SYMBOL(deeplog_pt_memdump);

// deprecated
unsigned long deeplog_pt_get_rebase(void)
{
	// return get_pt_rebase();
	return 0;
}
EXPORT_SYMBOL(deeplog_pt_get_rebase);


/* ==============================================
	Micro-bench evaluations
   ============================================== */
void micro_vmcall_PT_config(struct kvm_vcpu *vcpu)
{
	int ret;
	u64 data;
	
	/* read RTIT_CTL */
	if ((ret = kvm_get_msr(vcpu, MSR_IA32_RTIT_CTL, &data)) == 1) {
		deeplog_log_error("kvm(vmx)_get_msr(MSR_IA32_RTIT_CTL) failed.\n");
	}
	
	/* pause PT */
	data &= ~RTIT_CTL_TRACEEN;
	if ((ret = kvm_set_msr(vcpu, MSR_IA32_RTIT_CTL, data)) == 1) {
		deeplog_log_error("kvm(vmx)_set_msr(MSR_IA32_RTIT_CTL) failed.\n");
	}
}
EXPORT_SYMBOL(micro_vmcall_PT_config);
