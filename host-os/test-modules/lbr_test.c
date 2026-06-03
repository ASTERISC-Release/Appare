#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/kvm_para.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cookie");
MODULE_DESCRIPTION("Architectural LBR Test");
MODULE_VERSION("0.1");

/* Architectural LBR MSR definitions */
#define MSR_ARCH_LBR_CTL        0x000014ce
#define MSR_ARCH_LBR_DEPTH      0x000014cf
#define MSR_ARCH_LBR_FROM_0     0x00001500
#define MSR_ARCH_LBR_TO_0       0x00001600  
#define MSR_ARCH_LBR_INFO_0     0x00001200

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

/* Helper macros */
#define ARCH_LBR_ALL_BRANCHES   (ARCH_LBR_CTL_JCC | ARCH_LBR_CTL_REL_JMP | \
                                ARCH_LBR_CTL_IND_JMP | ARCH_LBR_CTL_REL_CALL | \
                                ARCH_LBR_CTL_IND_CALL | ARCH_LBR_CTL_RETURN | \
                                ARCH_LBR_CTL_OTHER_BRANCH)

#define DEFAULT_LBR_DEPTH       16

/* LBR entry structure */
struct my_lbr_entry {
    u64 from_ip;
    u64 to_ip;
    u64 info;
};

static struct proc_dir_entry *lbr_proc_entry;
static bool lbr_enabled = false;
static u32 lbr_depth = 0;

/* Check if Architectural LBR is supported */
static bool arch_lbr_supported(void)
{
    u32 eax, ebx, ecx, edx;
    
    /* Check CPUID.07H:EDX.arch-lbr (bit 19) */
    cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
    if (!(edx & (1 << 19))) {
        printk(KERN_INFO "LBR: Architectural LBR not supported in CPUID.07H:EDX\n");
        return false;
    }
    
    /* Check CPUID.1CH for LBR capabilities */
    cpuid_count(0x1c, 0, &eax, &ebx, &ecx, &edx);
    printk(KERN_INFO "LBR: CPUID.1CH - EAX=0x%x, EBX=0x%x, ECX=0x%x, EDX=0x%x\n", 
           eax, ebx, ecx, edx);
    
    return true;
}

/* Get LBR depth from CPUID */
static u32 get_arch_lbr_depth(void)
{
    u32 eax, ebx, ecx, edx;
    u32 bitmap;
    u32 host_lbr_depth;

    cpuid_count(0x1c, 0, &eax, &ebx, &ecx, &edx);
    bitmap = eax & 0xFF;  /* Bits 7:0 contain LBR depth */
    for (int i = 7; i >= 0; --i) {
        if (bitmap & (1u << i)) {
            host_lbr_depth = (i + 1) * 8; // 8,16,...,64
            break;
        }
    }
    return host_lbr_depth;
}

/* Enable Architectural LBR for ring-0 only */
static int enable_arch_lbr(void)
{
    u64 lbr_ctl = 0;
    u64 depth;
    
    if (!arch_lbr_supported()) {
        printk(KERN_ERR "LBR: Architectural LBR not supported\n");
        return -ENODEV;
    }
    
    lbr_depth = get_arch_lbr_depth();
    if (lbr_depth == 0) {
        printk(KERN_ERR "LBR: Invalid LBR depth\n");
        return -EINVAL;
    }
    
    printk(KERN_INFO "LBR: Detected LBR depth: %u\n", lbr_depth);
    
    /* Set LBR depth */
    // wrmsrl(MSR_ARCH_LBR_DEPTH, lbr_depth);

    /* Check depth */
    rdmsrl(MSR_ARCH_LBR_DEPTH, depth);
    printk(KERN_INFO "LBR: Current LBR (IA32_LBR_DEPTH) depth: %llu\n", depth);
    
    /* Configure LBR control for ring-0 only tracking */
    lbr_ctl = 0;
    lbr_ctl |= ARCH_LBR_CTL_ENABLE;        /* Enable LBR */
    lbr_ctl |= ARCH_LBR_CTL_KERNEL;        /* Capture kernel branches only */

    /* Enable all branch types for comprehensive tracking */
    lbr_ctl |= ARCH_LBR_ALL_BRANCHES;
    
    /* Write LBR control */
    wrmsrl(MSR_ARCH_LBR_CTL, lbr_ctl);
    
    lbr_enabled = true;
    
    printk(KERN_INFO "LBR: Enabled Architectural LBR (ring-0 only, depth=%llu, ctl=0x%llx)\n", 
           depth, lbr_ctl);
    
    return 0;
}

/* Disable Architectural LBR */
static void disable_arch_lbr(void)
{
    if (lbr_enabled) {
        wrmsrl(MSR_ARCH_LBR_CTL, 0);
        lbr_enabled = false;
        printk(KERN_INFO "LBR: Disabled Architectural LBR\n");
    }
}

/* Read LBR entries */
static int read_lbr_entries(struct my_lbr_entry *entries, int max_entries)
{
    int i, count = 0;
    u64 current_depth;
    
    // if (!lbr_enabled) {
    //     return 0;
    // }
    
    /* Read current depth */
    rdmsrl(MSR_ARCH_LBR_DEPTH, current_depth);
    current_depth = min_t(u64, current_depth, max_entries);
    
    /* Read all LBR entries */
    for (i = 0; i < current_depth; i++) {
        u64 from_ip = 0, to_ip = 0, info = 0;
        
        rdmsrl(MSR_ARCH_LBR_FROM_0 + i, from_ip);
        rdmsrl(MSR_ARCH_LBR_TO_0 + i, to_ip);
        rdmsrl(MSR_ARCH_LBR_INFO_0 + i, info);
        
        /* Skip empty entries */
        if (from_ip == 0 && to_ip == 0) {
            continue;
        }
        
        entries[count].from_ip = from_ip;
        entries[count].to_ip = to_ip;
        entries[count].info = info;
        count++;
    }
    
    return count;
}

/* Proc file show function */
static int lbr_proc_show(struct seq_file *m, void *v)
{
    struct my_lbr_entry entries[32];
    int count, i;
    u64 lbr_ctl, depth;
    
    seq_printf(m, "=== Guest Architectural LBR Status ===\n");
    
    if (!arch_lbr_supported()) {
        seq_printf(m, "Architectural LBR not supported\n");
        return 0;
    }
    
    rdmsrl(MSR_ARCH_LBR_CTL, lbr_ctl);
    rdmsrl(MSR_ARCH_LBR_DEPTH, depth);
    
    seq_printf(m, "LBR Enabled: %s\n", lbr_enabled ? "Yes" : "No");
    seq_printf(m, "LBR Control: 0x%llx\n", lbr_ctl);
    seq_printf(m, "LBR Depth: %llu\n", depth);
    
    if (!lbr_enabled) {
        seq_printf(m, "\nLBR not enabled. Load module with LBR enabled.\n");
        // return 0;
    }
    
    count = read_lbr_entries(entries, ARRAY_SIZE(entries));
    
    seq_printf(m, "\n=== LBR Entries (%d total) ===\n", count);
    
    for (i = 0; i < count; i++) {
        seq_printf(m, "[%2d] 0x%016llx -> 0x%016llx (info: 0x%llx)\n", 
                   i, entries[i].from_ip, entries[i].to_ip, entries[i].info);
    }
    
    if (count == 0) {
        seq_printf(m, "No LBR entries found.\n");
    }
    
    return 0;
}

static int lbr_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, lbr_proc_show, NULL);
}

static const struct proc_ops lbr_proc_ops = {
    .proc_open = lbr_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/* Function to generate some branch activity for testing */
static void generate_branch_activity(void)
{
    volatile int i, sum = 0;
    
    /* Generate conditional branches */
    for (i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            sum += i;
        } else {
            sum -= i;
        }
    }
    
    /* Generate function calls */
    printk(KERN_INFO "LBR: Generated branch activity (sum=%d)\n", sum);
}

static int __init test_module_init(void) {
    int ret;
    
    printk(KERN_INFO "LBR: Loading Guest Architectural LBR Test Module\n");
    
    /* Check if running in a VM */
    if (!kvm_para_available()) {
        printk(KERN_WARNING "LBR: Not running in KVM, but continuing anyway\n");
    }
    
    /* Enable Architectural LBR */
    // ret = enable_arch_lbr();
    // if (ret) {
    //     printk(KERN_ERR "LBR: Failed to enable Architectural LBR: %d\n", ret);
    //     return ret;
    // }
    
    /* Create proc entry */
    lbr_proc_entry = proc_create("guest_lbr", 0444, NULL, &lbr_proc_ops);
    if (!lbr_proc_entry) {
        printk(KERN_ERR "LBR: Failed to create /proc/guest_lbr\n");
        disable_arch_lbr();
        return -ENOMEM;
    }
    
    /* Generate some branch activity */
    // generate_branch_activity();
    
    printk(KERN_INFO "LBR: Module loaded successfully. Use: cat /proc/guest_lbr\n");
    return 0;
}

static void __exit test_module_exit(void) {
    /* Remove proc entry */
    proc_remove(lbr_proc_entry);
    
    /* Disable LBR */
    disable_arch_lbr();
    
    printk(KERN_INFO "LBR: Exit lbr test module.\n");
}

module_init(test_module_init);
module_exit(test_module_exit);