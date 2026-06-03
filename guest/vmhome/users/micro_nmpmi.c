#include "tests.h"
#include <unistd.h>
#include <sys/types.h>

#define __NR_getpid 39
#define N_LOOP      1000000


int main(int argc, char **argv) {
    int pid;

    PT_enable();
    for (int i = 0; i < N_LOOP; i++) {
        pid = syscall(__NR_getpid);
    }  
    PT_disable();

    // should check host os's dmesg then
    dmesg_log_statistics();
    return 0;
}