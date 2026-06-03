# Host OS

This is our folder for modifications to the host kernel

You can use the scripts in here to both install the correct linux kernel version on your host as well as update the kvm inside of it on the fly!

## Pre-reqs

* Linux kernel build dependencies:
```bash
sudo apt-get build-dep linux linux-image-$(uname -r)
sudo apt-get install libncurses-dev gawk flex bison openssl libssl-dev dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf llvm
```

* An ability to improvise

* A fighting spirit

* Some understanding of how to update your BIOS

* A BIOS without secure boot!

## Installation

For the host OS, a special kernel flag named `CONFIG_DEEPLOG`(see the line 14 of `Kconfig`) will always be enabled.

In order to install your host kernel for the first time simply run `./build-kernel.sh`

From here you should have the correct linux kernel installed to `/boot` 

You will need to update your BIOS to make sure it can point to it when you restart your computer. Please add those steps as you do them :)

## KVM Installation

**IMPORTANT**: For each time crashing/rebooting the host OS, please run:

```
sudo ./rebuild-kvm.sh
```

**Install Deeplog-enabled (CF/DF profiling) KVM**

- **ASSUM.**: I assume all the sensitive process CF profilings are done. They should be under:
`guest/profiling/autostart/function/processing/kvm_data/...`.

- **ASSUM.**: I assume you are currently under this folder (i.e., `repo-path/host-os/`).

- **ASSUM.**: I assume you can connect to your guest OS later via `ssh deeplog` from the host.

- **IMPORTANT.**: 
The current CF-profiling-based KVM is only for a single process (program, e.g., nginx).
Before testing a different program, please do the following steps again to rebuild/reinstall the KVM.

- **IMPORTANT AGAIN.**: 
Please rebuild the KVM by following the steps below, for each program test.


**1.** We compile the CF profilings to the KVM template
```bash
python3 kvm_cf_template_gen.py <proc_name>  
```
Note: to generate <proc_name>'s profile, the folder `guest/profiling/autostart/function/processing/kvm_data/<proc_name>` and its UD2 pages should exist.

After executing the script, a CF profile template `template.h` will be located at `kvm/mmu/template.h`.
For example, we can run `python3 kvm_cf_template_gen.py nginx` to generate nginx's `template.h`.

Also, you can run `python3 kvm_cf_template_gen.py empty` to generate an empty `template.h`.
This empty template will include nothing (i.e., all functions are validated and there are no `UD2` pages).
This empty template illustrates an ideal performance overhead of our CF tracking (i.e., no divergence will be triggered).


**2.** We compile the KVM.
```bash
sudo ./rebuild-kvm.sh
```
(Please check the host `dmesg` to ensure KVM is built successfully without any errors.)

**3.** We start the guest OS and upload some initialization programs.
- In the one terminal (start guest OS).
We boot the guest OS.
```bash
cd ../guest
sudo ./start-vm.sh
```

- In the other termnial (in host OS).
We upload initialization programs to the guest OS.
```bash
scp -r ../guest/users/ deeplog:~
```

**4.** (In guest OS) We initialize EPT contexts based on the installed KVM CF profile.

```bash
cd users
# make all programs
make 
make module
# initialize EPTP contexts
./init_eptp_contexts
# PLEASE CHECK host-OS's dmesg to ensure all contexts are correctly inited.
# IF NOT, please follow 1-3 to rebuild the host KVM and do again.
```

After the following steps, all EPTP pointers and UD2 code pages are prepared inside the `EPT_LIST`.
We can use `VMFUNC` at the beginning of a syscall later to switch the current EPT.

**5.** (Performance evaluation)
Suppose we now evaluate the performance of CF-logging enabled nginx.
Before starting nginx, we should enable `logging`:

```bash
cd users
sudo insmod cf_logging_enable.ko
```
Now, the guest OS will (a) perform `VMFUNC` at the syscall beginnings of nginx, and (b) perform `VMCALL(0x82350)` at the end of the syscall, to stop PT (if it's enabled by UD2).

Please use that performance number (indicating `APPARE-ACF`; CF-logging) against the native performance.
To measure native performance, you need to do nothing but ensure the `cf_logging_enable.ko` is UNLOADED.
%
You can also enable both this and (DF) data tracking to show the end-to-end performance.

**6.** (Statistics) We observe the statistics after running a test.
In guest OS:
```bash
cd users
./statistics
```

After this, see the `sudo dmesg` on the host OS to get statistics (syscall numbers and trapped UD2 numbers).

If you want to log/unlog detailed UD2 in runtime, please comment the last `deeplog_log_info` in the following function.
```c
/* host-os/kvm/mmu/libept.c, line 777 */
void update_ud2_statistics(struct kvm_vcpu *vcpu, u64 rip, u64 eptp)
{
    int ctx_id, i;
    u64 *eptp_list = vcpu_eptp_list_fetch(vcpu);
    /* get current deeplog_ctx id */
    ctx_id = -1;
    for (i = 0; i < 511; i++) {
        if (eptp == eptp_list[i]) {
            ctx_id = i;
            break;
        }
    }
    /* update total_ud2_number */
    total_ud2_number += 1;
    /* print? */
    // deeplog_log_info("[UD2][ctx=%d] RIP (UD2 point): 0x%llx. current total number: 0x%llx.\n",
    //                 ctx_id, rip, total_ud2_number);
}
```
If that log_info is enabled, you don't need to execute the `./statistics` in guest, but only need to `dmesg` in host to get numbers.

**7.** (Others) KVM data protection setup

If you want to set up data regions for different syscalls/EPTs.
Please use that `vmcall` inside the guest OS:
```c
void vmcall_setup_eptp_data_protection(struct kvm_vcpu *vcpu,
                                        int ctx_id,
                                        u64 gpa,
                                        int write_protect);

vmcall(0x20002, syscall_id, gpa, write_protection);
```
Note: `write_protect=1` for write logging and `0` for full access.

**Important**: This vmcall can ONLY be performed after all EPTP contexts are set up correctly.
Please ensure you have done steps 1-4 before doing this!
