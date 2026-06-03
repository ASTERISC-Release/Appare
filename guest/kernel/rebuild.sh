#!/bin/bash -e

# Check correct arguments were passed
if [ "$#" != "1" ];
then 
  echo "Usage: ./rebuild.sh <native|clang|gclang>"
  exit 0
fi

# source environment variables
pushd ../ && source .env && popd

# Need to export so that gclang knows where built LLVM binaries are
export PATH=$PATH:$LLVM:$GCLANG

# make kernel
pushd linux
  if [ "$1" == "clang" ];
  then
    # removing this file for instrumentation testing 
    rm -rf arch/x86/kernel/head64.o

    echo "Build: Clang"
    export KCFLAGS="-fsanitize=tempsan"
    # export KAFLAGS="-fsanitize=tempsan"
    make CC=`pwd`/../../../llvm-14/build/bin/clang LD=`pwd`/../../../llvm-14/build/bin/ld.lld -j`nproc` 2>&1 | tee make.dump
  elif [ "$1" == "native" ];
  then
    echo "Build: Native"
    make -j`nproc` 2>&1 | tee make.dump
  fi
popd

# load the vm image (if not currently loaded)
./load-vmdisk.sh 

# install the kernel to the vm image
pushd linux
  sudo env PATH=$PATH make INSTALL_PATH=$VMDISKMOUNT/boot install
popd

# install the initramfs
sudo chroot $VMDISKMOUNT sudo update-initramfs -c -k 6.1.9

# unload the vm image
./unload-vmdisk.sh