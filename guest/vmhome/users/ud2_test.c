#include "tests.h"

extern void ud2_function(int *addr);

int global_val = 0;

static void __attribute__((aligned(0x1000))) target_function(int *addr) {
    // printf("Executing the target function!\n");
    *addr = 0x1234;
    return;
}

int main() {
    int nr;
    u64 src_gva_list[1], dst_gva_list[1];

    u64 *ud2_func_addr, *target_func_addr;
    ud2_func_addr = (u64 *)ud2_function;
    target_func_addr = (u64 *)target_function;

    src_gva_list[0] = (u64)ud2_func_addr;
    dst_gva_list[0] = (u64)target_func_addr;
    // target_function();

    printf("global_val: %d, ud2_func address: 0x%lx, target_func address: 0x%lx.\n", 
            global_val, (unsigned long)ud2_func_addr, (unsigned long)target_func_addr);
    // vmcall
    kvm_hypercall3(0x10006, (unsigned long)src_gva_list, (unsigned long)dst_gva_list, 1);

    /* ud2 function */
    printf("start ud2 function, global_val: %d.\n", global_val);
    ud2_function(&global_val);
    printf("end ud2 function, global_val: %d\n", global_val);
    return 0;
}



