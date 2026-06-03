#include "tests.h"

int main(int argc, char **argv) {
    
    kvm_hypercall3(KVM_DEEPLOG_INIT_DEEPLOG_CONTEXTS, 0, 0, 0);

    kvm_hypercall3(KVM_DEEPLOG_ACT_DEEPLOG_CONTEXTS, 0, 0, 0);
    // check dmesg later.
    return 0;
}