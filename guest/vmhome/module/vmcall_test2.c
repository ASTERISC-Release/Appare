#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/kvm_para.h>
// file for kmalloc
#include <linux/slab.h>
// file for virt_to_phys
#include <linux/mm.h>
#include <asm/io.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cookie");
MODULE_DESCRIPTION("Test hypercall.");
MODULE_VERSION("0.1");

#define HYPERCALL_TEST_NR 0x9999

static int __init test_module_init(void) {
    unsigned long *buffer, gpa;
    printk(KERN_INFO "Init hypercall test module.\n");

    /* malloc a kernel GVA buffer */
    buffer = (unsigned long *)kmalloc(4096, GFP_KERNEL);

    /* print its GVA */
    printk(KERN_INFO "[Deeplog guest] GVA of buffer: 0x%lx\n", (unsigned long)buffer);

    /* va to pa to get GPA */
    // gpa = virt_to_phys(buffer);
    // printk(KERN_INFO "[Deeplog guest] GPA of buffer: 0x%lx\n", gpa);

    /* write (access) to the buffer before setting its write-protection */
    // *buffer = 0x11111111;
    // printk(KERN_INFO "[Deeplog guest] buffer[0]: 0x%lx\n", *buffer);

    /* vmcall(0x9999, GVA) */
    /* During this hypercall, the VMM should set buffer's EPT permission as non-writable */
    kvm_hypercall3(HYPERCALL_TEST_NR, (unsigned long)buffer, 0, 0);
    // asm volatile("vmcall": =r "rax":)
    /* write (access) to the buffer after setting its write-protection */
    *buffer = 0x99999999;
    printk(KERN_INFO "[Deeplog guest] buffer[0]: 0x%lx\n", *buffer);    
 
    /* free the buffer */
    kfree(buffer);

    return 0;
}

static void __exit test_module_exit(void) {
    printk(KERN_INFO "Exit hypercall test module.\n");
}

module_init(test_module_init);
module_exit(test_module_exit);
