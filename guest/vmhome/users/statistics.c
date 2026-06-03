#include "tests.h"
#include <unistd.h>
#include <sys/types.h>


int main(int argc, char **argv) {
    PT_disable();

    // should check host os's dmesg then
    dmesg_log_statistics();
    return 0;
}