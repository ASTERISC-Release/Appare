#!/bin/bash
pushd kernel
./load-vmdisk.sh
popd
sudo cp -r vmdisk/mnt/home/seclog ./
sudo rm -rf vmhome
sudo mv seclog vmhome
sudo chown $USER vmhome
sudo chown -R $USER vmhome/*
pushd kernel
./unload-vmdisk.sh
popd
printf "==========================\n"
printf "Succesfully Copied VM home\n"
printf "==========================\n"
