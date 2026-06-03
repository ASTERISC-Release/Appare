#include "tests.h"
#include <unistd.h>
#include <sys/types.h>


#define N_LOOP          1000
#define VMCALL_WP_ID    0x82345

char *wp_page;

int main(int argc, char **argv) {
    int pid;
    uint64_t start, end, total;

    total = 0; 

    /* assign a write protection page */
    if(posix_memalign((void **)&wp_page, SZ_4KB, SZ_4KB) != 0) {
        printf("posix_memalign wp_page failed.\n");
        return -1;
    }
    memset(wp_page, 0, SZ_4KB);

    /* register write protection */
    kvm_hypercall3(VMCALL_WP_ID, (unsigned long)wp_page, 0, 0);

    /* incremental write access */
    for (int i = 0; i < N_LOOP; i++) {
        
        start = rdtscp();
        *(wp_page + i) = 'A';
        end = rdtscp();

        printf("rdtscp value=%ld.\n", end-start);
        total += (end - start);
    }
    
    printf("buf: %s.\n", wp_page);

    /* time */
    printf("avg=%d times rdtscp cycle value=%ld.\n", 
            N_LOOP, total / N_LOOP);
    return 0;
}