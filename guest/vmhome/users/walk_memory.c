#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <asm/kvm_para.h>
#include <linux/pfn.h>
// file for kmalloc
#include <linux/slab.h>
// file for virt_to_phys
#include <linux/mm.h>
#include <linux/smp.h>
#include <asm/io.h>

#include <linux/fs.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("cookie");
MODULE_DESCRIPTION("Walk all the pages so that way there are no page faults");
MODULE_VERSION("0.1");

extern unsigned long max_pfn;
extern unsigned long phys_base;

#define KVM_DEEPLOG_WALK_MEMORY    0x40003

/*
static inline long kvm_hypercall3(unsigned int nr, unsigned long p1,
				  unsigned long p2, unsigned long p3)
{
	long ret;
	asm volatile(
             "vmcall"
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3)
		     : "memory");
	return ret;
}
*/

static inline void walk_guest_memory_done(void)
{
    kvm_hypercall3(KVM_DEEPLOG_WALK_MEMORY, 0, 0, 0);
}

// [    0.000000] BIOS-provided physical RAM map:
// [    0.000000] BIOS-e820: [mem 0x0000000000000000-0x000000000009fbff] usable
// [    0.000000] BIOS-e820: [mem 0x000000000009fc00-0x000000000009ffff] reserved
// [    0.000000] BIOS-e820: [mem 0x00000000000f0000-0x00000000000fffff] reserved
// [    0.000000] BIOS-e820: [mem 0x0000000000100000-0x00000000bffdcfff] usable
// [    0.000000] BIOS-e820: [mem 0x00000000bffdd000-0x00000000bfffffff] reserved
// [    0.000000] BIOS-e820: [mem 0x00000000feffc000-0x00000000feffffff] reserved
// [    0.000000] BIOS-e820: [mem 0x00000000fffc0000-0x00000000ffffffff] reserved
// [    0.000000] BIOS-e820: [mem 0x0000000100000000-0x000000023fffffff] usable
// [    0.000000] NX (Execute Disable) protection: active

void walk_memory(void *dummy)
{
  unsigned long p;
  unsigned long page_size = 4096;
  unsigned long total_ram_pages = get_num_physpages();
  unsigned long total_ram = total_ram_pages * page_size;
  printk(KERN_INFO "Total RAM: %lu bytes\n", total_ram);

  for (p = 0; p < 0x9fbff; p += 0x1000) {
    // Calculate the address
    void *address = phys_to_virt((p));
    // Accessing the first byte of the page
    if (address)
      printk(KERN_INFO "First byte (address: 0%lx): 0x%x\n", (unsigned long)address, ((volatile char *)address)[0]);
  }

  for (p = 0x100000; p < 0xbffdcfff; p += 0x1000) {
    // Calculate the address
    void *address = phys_to_virt((p));
    // Accessing the first byte of the page
    if (address)
      printk(KERN_INFO "First byte (address: 0%lx): 0x%x\n", (unsigned long)address, ((volatile char *)address)[0]);
  }
  
  for (p = 0x100000000; p < 0x23fffffff; p += 0x1000) {
    // Calculate the address
    void *address = phys_to_virt((p));
    // Accessing the first byte of the page
    if (address)
      printk(KERN_INFO "First byte (address: 0%lx): 0x%x\n", (unsigned long)address, ((volatile char *)address)[0]);
  }
  printk(KERN_EMERG "Done walking memory on vCPU=%d.\n", smp_processor_id());
}

static int __init test_module_init(void) {
  on_each_cpu(walk_memory, NULL, 1);
  walk_guest_memory_done();
  return 0; // Non-zero return means that the module couldn't be loaded.
}

static void __exit test_module_exit(void) {
  asm("nop");
}

module_init(test_module_init);
module_exit(test_module_exit);
