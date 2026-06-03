#!/bin/bash -e

# Get environment variables
source .env

# Initial information
echo "----------------------------------"
echo "Function: This script restarts a stopped virtual machine"
echo ""
echo "  1. Mapping CTRL-C to CTRL-]"
echo "  2. Press CTRL-] to stop the VM"
echo "----------------------------------"
echo ""

# Disable huge pages
sudo sh -c "echo never > /sys/kernel/mm/transparent_hugepage/enabled"
sudo sh -c "echo never > /sys/kernel/mm/transparent_hugepage/defrag"

if test -d "vmhome"; then
	./copy-to-vm.sh
	sleep 0.5
fi

# Map CTRL-C to CTRL-]
stty intr ^]

# launch the QEMU VM
QEMU="../local-qemu/qemu-8.1.0/build/qemu-system-x86_64"
MEMORY=8192M

# Enable LBR support with proper CPU features
taskset -c 0,1,2,3,4,5,6,7 $QEMU -s -cpu host,intel-pt=on,pmu=on,+arch-lbr \
	  -enable-kvm \
	  -smp 8,maxcpus=8 \
	  -m $MEMORY -mem-prealloc \
	  -no-reboot \
	  -netdev user,id=vmnic,hostfwd=tcp::8000-:22,hostfwd=tcp::9000-:80,hostfwd=tcp::10000-:11211,hostfwd=tcp::11000-:6379 \
	  -device e1000,netdev=vmnic,romfile= \
	  -drive file=$VMDISK,if=none,id=disk0,format=qcow2 \
	  -device virtio-scsi-pci,id=scsi0,disable-legacy=on,iommu_platform=true \
	  -device scsi-hd,drive=disk0 \
	  -nographic \
	  -monitor pty \
	  -monitor unix:monitor,server,nowait

# restore the mapping
stty intr ^c

sleep 0.5
./copy-from-vm.sh

# grant vmhome ownership
if test -d "vmhome"; then
	ORIGINAL_USER="${SUDO_USER:-$USER}"
	echo "restore vmhome permission back to $ORIGINAL_USER:$ORIGINAL_USER"
	sudo chown -R $ORIGINAL_USER:$ORIGINAL_USER vmhome/
fi

