#include "tests.h"
#include <unistd.h>
#include <sys/types.h>

#define __NR_getpid 39
#define N_LOOP      1000


int main(int argc, char **argv) {
    int pid;
    uint64_t start, end;
    kvm_hypercall3(0x20005, 0, 0, 0);

    start = rdtsc();
    for (int i = 0; i < N_LOOP; i++) {
        pid = syscall(__NR_getpid);
    }
    end = rdtsc();

    kvm_hypercall3(0x20008, 0, 0, 0);
    /* time */
    printf("rdtsc value=%ld.\n", end-start);
    return 0;
}