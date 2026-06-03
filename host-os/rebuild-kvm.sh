#!/bin/bash

cp ./kvm/mmu/* ./linux-5.19/arch/x86/kvm/mmu/
cp ./kvm/vmx/* ./linux-5.19/arch/x86/kvm/vmx/
cp ./kvm/*.c   ./linux-5.19/arch/x86/kvm/
cp ./kvm/*.h   ./linux-5.19/arch/x86/kvm/

cp ./include-asm/*.h ./linux-5.19/arch/x86/include/asm/

pushd linux-5.19
make modules -j`nproc`

if [ $? -eq 0 ]
then
sudo rmmod kvm-intel
sudo rmmod kvm

sudo insmod ./arch/x86/kvm/kvm.ko
sudo insmod ./arch/x86/kvm/kvm-intel.ko pt_mode=1
fi

popd
