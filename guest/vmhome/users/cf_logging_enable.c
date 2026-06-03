#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <asm/kvm_para.h>
// file for kmalloc
#include <linux/slab.h>
// file for virt_to_phys
#include <linux/mm.h>
#include <asm/io.h>

#include <linux/fs.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cookie");
MODULE_DESCRIPTION("Enable anomaly control flow logging.");
MODULE_VERSION("0.1");
/*
 * IMPORTANT: 
 * remember to update this address when the kernel is updated.
 * sudo cat /proc/kallsyms | grep DEEPLOG_IS_CF_LOGGING
 * to see the address of DEEPLOG_IS_CF_LOGGING
 */
extern int DEEPLOG_IS_CF_LOGGING;

static int __init test_module_init(void) {
    
    printk(KERN_INFO "Origin val. %d.\n",
                        DEEPLOG_IS_CF_LOGGING);
    DEEPLOG_IS_CF_LOGGING = 1;
    printk(KERN_INFO "Enable anomaly CF logging on sensitive procs. %d.\n",
                        DEEPLOG_IS_CF_LOGGING);
    return 0;
}

static void __exit test_module_exit(void) {
    DEEPLOG_IS_CF_LOGGING = 0;
}

module_init(test_module_init);
module_exit(test_module_exit);
