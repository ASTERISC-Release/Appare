#include "tests.h"

int main(int argc, char **argv) {
    kvm_hypercall3(KVM_DEEPLOG_OBJ_PROFILING, 0, 0, 0);
    // check dmesg later.
    return 0;
}
