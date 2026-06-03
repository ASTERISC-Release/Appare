#include "tests.h"

static void test_vmfunc_cycle(int id) {
    uint64_t avg = 0;
    uint64_t start, end;
    for (int i = 0; i < 10; i++) {
        start = rdtsc();
        vmfunc(0, id);
        end = rdtsc();
        avg += (end - start);
    }
    printf("VMFUNC(rax=0, rcx=%d) cycle (10 times avg): %lu.\n", id, avg / 10);
}

char buf[64];
int global_val = 0;
int *p1, *p2;

static void mimic_subsystem_enter(void) {
    // subsystem: we first fill a 64 bytes buffer
    memset(buf, 'A', 64);
    // we then set global_val to *p1
    global_val = *p1;
    // we then set *p2 to global_val
    *p2 = global_val;
    // we then set *p1 to 0
    *p1 = 0;
    // we then set global_val to 0
    global_val = 0;
    // we then set *p1 to *p2
    *p1 = *p2;
}



int main(int argc, char **argv) {
    unsigned long spte;
    uint64_t start, end;

    int v1, v2;

    /* allocate two buffers */
    if(posix_memalign((void **)&p1, SZ_4KB, SZ_4KB) != 0) {
        printf("posix_memalign p1 failed.\n");
        return -1;
    }
    *p1 = 0x11111111;

    if(posix_memalign((void **)&p2, SZ_4KB, SZ_4KB) != 0) {
        printf("posix_memalign p2 failed.\n");
        return -1;
    }
    *p2 = 0x22222222;

    printf("p1 addr: 0x%lx, cont: 0x%x; p2 addr: 0x%lx, cont: 0x%x.\n", 
                    (unsigned long)p1, *p1, (unsigned long)p2, *p2);

    if (argc == 1) { /* initialization */
        // /* print spte in vmx */
        // spte = kvm_hypercall3(0x10001, (unsigned long)p1, 0, 0);
        // spte = kvm_hypercall3(0x10001, (unsigned long)p2, 0, 0);
    
        /* we now set 2 ept2, in the second ept, we map p1 to p2 */
        printf("init EPTs.\n");
        kvm_hypercall3(0x10002, (unsigned long)p1, (unsigned long)p2, 0);
    
        /* we check eptp status */
        kvm_hypercall3(0x10004, 0, 0, 0);

        // switch to 0
        vmfunc(0, 0);
        kvm_hypercall3(0x10004, 0, 0, 0); /* we check eptp status */
        v1 = *p1;
        v2 = *p2;
        printf("after switch to eptp_list[0], *p1: 0x%x, *p2: 0x%x.\n", v1, v2);

        vmfunc(0, 2);
        kvm_hypercall3(0x10004, 0, 0, 0); /* we check eptp status */
        v1 = *p1;
        v2 = *p2;
        printf("after switch to eptp_list[2], *p1: 0x%x, *p2: 0x%x.\n", v1, v2);
        
        
        vmfunc(0, 3);
        kvm_hypercall3(0x10004, 0, 0, 0); /* we check eptp status */
        v1 = *p1;
        v2 = *p2;
        printf("after switch to eptp_list[3], *p1: 0x%x, *p2: 0x%x.\n", v1, v2);
        
        
        vmfunc(0, 4);
        kvm_hypercall3(0x10004, 0, 0, 0); /* we check eptp status */
        v1 = *p1;
        v2 = *p2;
        printf("after switch to eptp_list[4], *p1: 0x%x, *p2: 0x%x.\n", v1, v2);

        vmfunc(0, 0);
    } 

    else if (argc == 2) {  /* native performance */
        printf("before switch to second ept, *p1: 0x%x, *p2: 0x%x.\n", *p1, *p2);
        // switch
        vmfunc(0, 3);
        printf("after switch to second ept, *p1: 0x%x, *p2: 0x%x.\n", *p1, *p2);
        printf("test done.\n");
    }
    
    else if (argc == 3){                 /* vmfunc performance */
        vmfunc(0, 5);
        vmfunc(0, 0);
    }
    
    else if (argc > 3) {
        for (int i = 0; i < 6; i++){
            test_vmfunc_cycle(i);
        }
    }

    vmfunc(0, 0);
    /* free p1 and p2 */
    // free(p1);
    // free(p2);
    return 0;
}