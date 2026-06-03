#!/bin/bash
pushd kernel
./load-vmdisk.sh
popd
sudo cp -r vmhome vmdisk/mnt/home/
sudo rm -rf vmdisk/mnt/home/seclog
sudo mv vmdisk/mnt/home/vmhome vmdisk/mnt/home/seclog
sudo chown 1000 vmdisk/mnt/home/seclog
sudo chown -R 1000 vmdisk/mnt/home/seclog/
pushd kernel
./unload-vmdisk.sh
popd
printf "=====================================\n"
printf "Succesfully Copied VM home into guest\n"
printf "=====================================\n"
