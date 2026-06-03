#!/bin/bash -e

# source environment variables
pushd ../ && source .env && popd

ROOT=${PWD}
LLVMPREFIX=${ROOT}/'../../llvm-14/build'

CLANG="ccache ${LLVMPREFIX}/bin/clang"
CC=${LLVMPREFIX}/bin/clang
CXX=${LLVMPREFIX}/bin/clang++
OPT=${LLVMPREFIX}/bin/opt
LD=${LLVMPREFIX}/bin/ld.lld
LLVM_NM=${LLVMPREFIX}/bin/llvm-nm
LLVM_AR=${LLVMPREFIX}/bin/llvm-ar
LLVM_STRIP=${LLVMPREFIX}/bin/llvm-strip
LLVM_OBJCOPY=${LLVMPREFIX}/bin/llvm-objcopy
LLVM_OBJDUMP=${LLVMPREFIX}/bin/llvm-objdump
LLVM_READELF=${LLVMPREFIX}/bin/llvm-readelf
LLVM_LINK=${LLVMPREFIX}/bin/llvm-link
LLVM_CONFIG=${LLVMPREFIX}/bin/llvm-config

# load the vm image (if not currently loaded)
./load-vmdisk.sh
sleep 2

# guest Linux version
GUEST_LINUX_VER=6.1.9

# unlink the build directory (if exists)
sudo unlink $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build || true
sudo unlink $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build/linux || true
sudo rm -rf $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build

# install all modules
pushd linux
make LLVM=1 modules -j$(nproc)
sudo env PATH=$PATH make INSTALL_MOD_PATH=$VMDISKMOUNT modules_install -j$(nproc)
echo "Installed Modules"
popd

# install the kernel to the vm image
pushd linux
sudo env PATH=$PATH make INSTALL_PATH=$VMDISKMOUNT/boot install
popd

# install the initramfs
sudo chroot $VMDISKMOUNT sudo update-initramfs -c -k ${GUEST_LINUX_VER}
sudo chroot $VMDISKMOUNT sudo update-grub

# unload the vm image
sleep 2
./unload-vmdisk.sh

./copy-build-source.sh
