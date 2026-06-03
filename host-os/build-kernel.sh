#!/bin/bash

LINUX_DIR="linux-5.19"

# prepare kernel sources
if [ ! -d $LINUX_DIR ]; then
    wget https://mirrors.edge.kernel.org/pub/linux/kernel/v5.x/linux-5.19.tar.xz
    tar -xf linux-5.19.tar.xz
    rm linux-5.19.tar.xz
fi

# replace the DeepLog-enabled Kconfig
cp Kconfig ./$LINUX_DIR/arch/

# prepare kernel configs
cp config-5.19.0 ./$LINUX_DIR/.config

# prepare modified KVM sources
cp ./kvm/mmu/* ./$LINUX_DIR/arch/x86/kvm/mmu/
cp ./kvm/vmx/* ./$LINUX_DIR/arch/x86/kvm/vmx/
cp ./kvm/*.c   ./$LINUX_DIR/arch/x86/kvm/
cp ./kvm/*.h   ./$LINUX_DIR/arch/x86/kvm/

# build kernel
pushd $LINUX_DIR
make -j`nproc`
sudo make -j`nproc` modules_install
sudo make -j`nproc` install
popd


