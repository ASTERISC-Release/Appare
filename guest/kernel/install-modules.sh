#!/bin/bash -e

# source environment variables
pushd ../ && source .env && popd

# load the vmdisk (if not currently loaded)
./load-vmdisk.sh
sleep 2

# guest Linux version
GUEST_LINUX_VER=$KERNELVERSION

# unlink the build directory (if exists)
sudo unlink $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build || true
sudo unlink $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build/linux || true
sudo rm -rf $VMDISKMOUNT/lib/modules/$GUEST_LINUX_VER/build

# install the kernel modules to the vmdisk
pushd linux
  sudo env PATH=$PATH make INSTALL_MOD_PATH=$VMDISKMOUNT \
    modules_install -j`nproc`
popd

# unload the vmdisk
./unload-vmdisk.sh
