#include "tests.h"

/*
 * ud2_function:
 *  ud2
 *  ret
 */
extern void ud2_function(void);

#define N_LOOP      1000

int global_val = 0;

int main(int argc, char **argv) {
    int pid;
    uint64_t start, end, total;

    total = 0; 

    for (int i = 0; i < N_LOOP; i++) {

        start = rdtscp();
        ud2_function();
        end = rdtscp();
        
        printf("rdtscp value=%ld.\n", end-start);
        total += (end - start);
    }
    
    /* time */
    printf("avg=%d times rdtscp cycle value=%ld.\n", 
            N_LOOP, total / N_LOOP);
    return 0;
}


