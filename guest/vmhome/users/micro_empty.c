#include "tests.h"
#include <unistd.h>
#include <sys/types.h>


#define N_LOOP      1000
#define VMCALL_EMPTY_ID    0x82347

int main(int argc, char **argv) {
    int pid;
    uint64_t start, end, total;

    total = 0; 

    for (int i = 0; i < N_LOOP; i++) {

        start = rdtscp();
        kvm_hypercall3(VMCALL_EMPTY_ID, 0, 0, 0);
        end = rdtscp();
        
        printf("rdtscp value=%ld.\n", end-start);
        total += (end - start);
    }
    
    /* time */
    printf("avg=%d times rdtscp cycle value=%.2f.\n", 
            N_LOOP, (float)total / N_LOOP);
    return 0;
}