#ifndef __KVM_X86_VMX_COMMON_H
#define __KVM_X86_VMX_COMMON_H

/* VCPU information */
#define VCPU_MAX	8

/* debug log function */
#ifndef deeplog_log_info
#define deeplog_log_info(fmt, arg...) \
    printk(KERN_INFO "[DeepLog][%s][%d] "pr_fmt(fmt)"", __func__, __LINE__, ##arg)
#endif

#ifndef deeplog_log_error
#define deeplog_log_error(fmt, arg...) \
    printk(KERN_EMERG "[DeepLog][%s][%d] "pr_fmt(fmt)"", __func__, __LINE__, ##arg)
#endif

/* CPU cycle */
static inline unsigned long dl_rdtscp(void) {
    unsigned int lo, hi;
    asm volatile(
        "rdtscp"
        : "=a"(lo), "=d"(hi)
        :
        : "memory"
    );
    return ((unsigned long)hi << 32) | lo;
}

/* page fault after memory pinning debug */
extern int is_deeplog_memory_pinned;
#define DL_NEVER_HAPPEN(x, fmt, arg...) \
do { \
	if ((x)) { \
		deeplog_log_error(fmt, ##arg); \
	} \
} while (0)

/* debug assert */
#define DEEPLOG_DEBUG

#define UNHANDLED(x)      \
do {					  \
	if ((x)) {							\
		printk(KERN_EMERG "[DeepLog] unhandled %s: %d: %s\n",	\
			__FILE__, __LINE__, #x);      \
	}								   \
} while (0)

#ifdef DEEPLOG_DEBUG
#define DL_ASSERT(x)  							\
do {									\
	if (!(x)) {							\
		printk(KERN_EMERG "[DeepLog] assertion failed %s: %d: %s\n",	\
		       __FILE__, __LINE__, #x);				\
		BUG();							\
	}								\
} while (0)
#define pt_debug(fmt, ...) deeplog_log_info(fmt, ## __VA_ARGS__)
#define NEVER(x)                        \
do {									\
	if ((x)) {							\
		printk(KERN_EMERG "[DeepLog] Never failed %s: %d: %s\n",	\
		       __FILE__, __LINE__, #x);				\
	}                                   \
} while (0)
#else
#define DL_ASSERT(x) do { } while (0)
#define pt_debug(fmt, ...)
#define NEVER(x)
#endif

#endif