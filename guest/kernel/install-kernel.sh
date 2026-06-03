#!/bin/bash -e

# Check correct arguments were passed
# if [ "$#" != "1" ]; then
# 	echo "Usage: ./install-kernel.sh"
# 	exit 0
# fi

# source environment variables
pushd ../ && source .env && popd

function check_kernel_config {
	# # Disable 32-bit emulation since our LLVM pass is not configured for 32-bit
	# sed -i "s/CONFIG_IA32_EMULATION=y/CONFIG_IA32_EMULATION=n/g" .config

	# # Enable kernel debugging using GDB
	# sed -i "s/CONFIG_RANDOMIZE_BASE=y/CONFIG_RANDOMIZE_BASE=n/g" .config

	# # Enable kernel debugging using GDB
	# sed -i "s/CONFIG_NODES_SHIFT=6/CONFIG_NODES_SHIFT=9/g" .config

	# # Enable cma allocation
	# # sed -i "s/# CONFIG_CMA is not set/CONFIG_CMA=y\nCONFIG_CMA_DEBUG=y\nCONFIG_CMA_DEBUGFS=y\nCONFIG_CMA_SYSFS=y\nCONFIG_CMA_AREAS=2\nCONFIG_DMA_CMA=y\nCONFIG_DMA_PERNUMA_CMA=n\nCONFIG_CMA_SIZE_PERCENTAGE=1\nCONFIG_CMA_SIZE_SEL_MBYTES=n\nCONFIG_CMA_SIZE_SEL_MIN=n\nCONFIG_CMA_SIZE_SEL_MAX=n\nCONFIG_CMA_SIZE_SEL_PERCENTAGE=y\nCONFIG_CMA_ALIGNMENT=8/g" .config

	# # Disable retpoline
	# sed -i "s/CONFIG_SPECULATION_MITIGATIONS=y/CONFIG_SPECULATION_MITIGATIONS=n/g" .config
	# sed -i "s/CONFIG_RETPOLINE=y/CONFIG_RETPOLINE=n/g" .config

	# Print and show (please add more later)
	echo ""
	echo "Using the following configurations:"
	echo "-----------------------------------"
	cat .config | grep "CONFIG_IA32_EMULATION"
	cat .config | grep "CONFIG_RANDOMIZE_BASE"
	# cat .config | grep "CONFIG_DMA_CMA"
	cat .config | grep "CONFIG_SPECULATION_MITIGATIONS"
	cat .config | grep "CONFIG_RETPOLINE" || true
	cat .config | grep "CONFIG_NODES_SHIFT"
	cat .config | grep "CONFIG_DEBUG_INFO"
	cat .config | grep "CONFIG_LIVEPATCH" || true
	cat .config | grep "CONFIG_FUNCTION_TRACER" || true
	echo "-----------------------------------"
	echo ""
	sleep 2
}

# make kernel
pushd linux
	echo "Build: kernel"
	# make defconfig
	cp ../kernel_config .config
 	check_kernel_config
	make -j$(nproc)
popd

# install all modules
./install-modules.sh

# copy source for kernel module
./copy-build-source.sh

# load the vm image (if not currently loaded)
./load-vmdisk.sh

# install the kernel to the vm image
pushd linux
# sudo env PATH=$PATH make INSTALL_PATH=$VMDISKMOUNT/boot CC=clang install
sudo env PATH=$PATH make INSTALL_PATH=$VMDISKMOUNT/boot install
# CC=`pwd`/../../../llvm-new-version/build/bin/clang install
popd

# install the initramfs
sudo chroot $VMDISKMOUNT sudo update-initramfs -c -k 6.1.9

# unload the vm image
./unload-vmdisk.sh
