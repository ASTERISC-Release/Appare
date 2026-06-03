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

int g_variable = 0;

static inline void vmfunc(u64 input_rax, u64 input_rcx) {
    asm volatile(
        "vmfunc"
        :
        : "a"(input_rax), "c"(input_rcx)
        : "memory"
    );
}

int *p1, *p2;
struct page *page1, *page2;

static int __init test_module_init(void) {   
    int v1, v2; 
    page1 = alloc_page(GFP_KERNEL | __GFP_ZERO);
    page2 = alloc_page(GFP_KERNEL | __GFP_ZERO);
    p1 = (int *)page_to_virt(page1);
    p2 = (int *)page_to_virt(page2);

    *p1 = 0x11111111;
    *p2 = 0x22222222;
    v1 = *p1;
    v2 = *p2;
    printk(KERN_INFO "p1 addr: 0x%lx, cont: 0x%x; p2 addr: 0x%lx, cont: 0x%x.\n", 
                    (unsigned long)p1, v1, (unsigned long)p2, v2);
    /* we now set 2 ept2, in the second ept, we map p1 to p2 */
    printk(KERN_INFO "init EPTs.\n");
    kvm_hypercall3(0x10002, (unsigned long)p1, (unsigned long)p2, 0);
    
    /* we check eptp status */
    kvm_hypercall3(0x10004, 0, 0, 0);
    
    // switch to 0
    vmfunc(0, 0);
    kvm_hypercall3(0x10004, 0, 0, 0); /* we check eptp status */
    v1 = *p1;
    v2 = *p2;
    printk(KERN_INFO "after switch to eptp_list[0], *p1: 0x%x, *p2: 0x%x.\n", v1, v2);

    vmfunc(0, 2);
    kvm_hypercall3(0x10004, 0, 0, 0); /* we check eptp status */
    v1 = *p1;
    v2 = *p2;
    printk(KERN_INFO "after switch to eptp_list[2], *p1: 0x%x, *p2: 0x%x.\n", v1, v2);
    
    
    vmfunc(0, 3);
    kvm_hypercall3(0x10004, 0, 0, 0); /* we check eptp status */
    v1 = *p1;
    v2 = *p2;
    printk(KERN_INFO "after switch to eptp_list[3], *p1: 0x%x, *p2: 0x%x.\n", v1, v2);
    
    
    vmfunc(0, 4);
    kvm_hypercall3(0x10004, 0, 0, 0); /* we check eptp status */
    v1 = *p1;
    v2 = *p2;
    printk(KERN_INFO "after switch to eptp_list[4], *p1: 0x%x, *p2: 0x%x.\n", v1, v2);

    vmfunc(0, 0);
    return 0;
}

static void __exit test_module_exit(void) {
    printk(KERN_INFO "Exit test module.\n");
    __free_page(page1);
    __free_page(page2);
}

module_init(test_module_init);
module_exit(test_module_exit);
