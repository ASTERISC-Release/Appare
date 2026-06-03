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

static void ___dump(void) {
    kvm_hypercall3(0x40005, 0, 0, 0);
}

static int __init test_module_init(void) {
    printk(KERN_INFO "LBR: Loading Guest Architectural LBR Test Module\n");
    ___dump();
    return 0;
}

static void __exit test_module_exit(void) {
    printk(KERN_INFO "LBR: Exit lbr test module.\n");
}

module_init(test_module_init);
module_exit(test_module_exit);