#!/bin/bash -e

QEMU_DIR="qemu-8.1.0"

if [ ! -d $QEMU_DIR ]; then
    wget https://download.qemu.org/qemu-8.1.0.tar.xz
    if [ $? -ne 0 ]; then
        echo "Error downloading QEMU. Check again."
        exit
    fi
    tar xvJf qemu-8.1.0.tar.xz
    rm qemu-8.1.0.tar.xz
    patch -p0 -i patch-qemu-pt.diff
fi

cd qemu-8.1.0
./configure --enable-slirp --target-list=x86_64-softmmu
make -j`nproc`

echo "-------------------------------------"
echo "QEMU (PT) has been compiled, the build is located in qemu-8.1.0/build"
echo "-------------------------------------"
