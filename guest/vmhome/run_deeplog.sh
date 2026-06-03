#!/bin/bash

if [[ -z $1 ]]; then
	echo "Usage: run_deeplog.sh cf | obj | full"
	exit 1
fi

cd users
echo "|-----------------------|"
echo "|   Building Scripts    |"
echo "|-----------------------|"
make clean
make
make module
make mem
# test my vmcall here
echo "/-------------------------\\"
echo "| Pinning memory in host  |"
echo "\\------------------------/"
./pin_memory
echo "/-----------------------\\"
echo "| Forcing EPT Faults     |"
echo "\\-----------------------/"
# This should be both from the guest and the host
sudo insmod walk_memory.ko
if [[ $1 == "cf" ]] || [[ $1 == "full" ]]; then
	echo ".-----------------------."
	echo "| Initializing EPT-P's  |"
	echo "'-----------------------'"
	./init_eptp_contexts
fi

if [[ $1 == "obj" ]] || [[ $1 == "full" ]]; then
	echo ".-----------------------."
	echo "| Initializing Obj-P's  |"
	echo "'-----------------------'"
	./init_obj_contexts
fi

echo ".-----------------------."
echo "|   Activating Obj-P's  |"
echo "'-----------------------'"
./activate_eptp_contexts

echo "*---------------------------*"
echo "| Activating Guest Contexts |"
echo "*---------------------------*"

sudo insmod cf_logging_enable.ko

if [[ $1 == "obj" ]] || [[ $1 == "full" ]]; then
	cd ..
	cd objprofile
	make
	sudo insmod objprofile.ko
fi

echo "#------------------------#"
echo "| Ready for Testing      |"
echo "#------------------------#"
