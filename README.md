# APPARE

APPARE audits Linux kernel control flow. It derives per-syscall reference behaviors using dynamic profiling and LLM-assisted static analysis, then uses KVM, Intel Processor Trace (PT), and LBR to record execution that diverges from those behaviors.

**Paper:** [Fine-Grained Kernel Auditing using Augmented Syscall Reference Behavior Analysis and Virtualized Selective Tracing](https://www.chuqiz.com/papers/appare_paper.pdf).

## Components

| Component | Location | Purpose |
| --- | --- | --- |
| Dynamic profiler | [`guest/vmhome/kftracer/`](guest/vmhome/kftracer/) | Record ftrace traces, extract per-syscall function sets, and generate UD2 code maps. |
| Static/LLM profiler | [`guest/kern-cfg/`](guest/kern-cfg/) | Expand profiles using static call graphs, kernel source, and Ollama. Includes precomputed inputs and results. |
| Guest kernel | [`guest/kernel/`](guest/kernel/) | Modified Linux 6.1.9 with syscall-entry/exit instrumentation; build and VM-image installation scripts. |
| Logger | [`host-os/`](host-os/) | Modified Linux 5.19/KVM: restricted EPT code views, divergence handling, PT buffering, and LBR support. |
| VM tooling | [`guest/`](guest/), [`local-qemu/`](local-qemu/) | Ubuntu image management and patched QEMU 8.1.0. |
| Guest controls | [`guest/vmhome/users/`](guest/vmhome/users/) | Initialize logging, enable/disable tracing, collect statistics, and run microbenchmarks. |
| Parser | [`host-os/pt-tools/pt/`](host-os/pt-tools/pt/) | Decode PT logs into basic-block/syscall CSVs using diStorm and matching kernel code images. |
| Evaluations | [`server-evals/`](server-evals/) | Nginx, Redis, Memcached, Phoronix, auditd comparisons, and plotting utilities. |

The pipeline is **workloads → reference profiles → UD2 maps → KVM logger → PT logs → decoder**. Source symbols and scripts often retain the development name **DeepLog**.

## Setup

Use an **Intel x86-64 Linux host** supporting KVM, EPT/VMFUNC, Intel PT, and architectural LBR. Scripts assume Ubuntu/Debian, `sudo`, and a host that can load custom kernels/modules. Defaults are **8 vCPUs, 8 GiB guest RAM, and a 50 GiB disk**. Build dependencies are listed in the [host](host-os/README.md) and [QEMU](local-qemu/README.md) notes.

Run scripts from their containing directories. On the host, starting at the repository root:

```bash
export APPARE="$(pwd)"
cd "$APPARE/local-qemu"
./install-qemu-pt.sh
cd "$APPARE/host-os"
cp include-asm/*.h linux-5.19/arch/x86/include/asm/
./build-kernel.sh
```

Reboot into the installed **5.19.0 host kernel**. With other KVM guests stopped:

```bash
cd "$APPARE/host-os"
sudo ./rebuild-kvm.sh
cd "$APPARE/guest"
./create-vmdisk.sh
# Finish initial provisioning and shut down the guest before installing its kernel.
cd kernel
./install-kernel.sh
./load-vmdisk.sh
sudo chroot "$APPARE/guest/vmdisk/mnt" update-grub
./unload-vmdisk.sh
cd ..
sudo ./start-vm.sh
```

Boot the guest's **6.1.9 kernel**. Review [`guest/.env`](guest/.env) for image/account settings; provision SSH authentication for `seclog` (bootstrap removes its password). Connect with `ssh -p 8000 seclog@localhost`.

Host ports **8000/9000/10000/11000** forward to guest **SSH/HTTP/Memcached/Redis**. The launcher copies `guest/vmhome/` into `/home/seclog` before boot and copies it back after exit, replacing the destination directory. Keep the VM stopped during offline disk operations.

## Profiler

### Collect dynamic profiles

Inside the guest, install `trace-cmd`, then record and process a workload:

```bash
cd ~/kftracer
./trace-command.sh ls-1.dat /bin/ls /tmp
./report.sh ls ls-1.dat
python3 syscall-kfunc-parser.py ls 1
python3 syscall-stat-same-prog.py ls
```

For an existing server, use `./trace-server.sh nginx nginx-1.dat`, generate traffic, then stop recording with Ctrl-C and process it the same way. Use the exact process name and distinct run IDs. Outputs are `syscall_profiles/<id>:<name>/<program>-all.txt` and `executed_syscalls/<program>.txt`.

### Expand profiles with an LLM

On the analysis machine, install Python packages `networkx matplotlib scipy tqdm ollama`, start [Ollama](https://ollama.com/), and pull `qwen3:32b`:

```bash
ollama pull qwen3:32b
cd "$APPARE/guest/kern-cfg"
python3 run-slice-client.py --port 11434 --syscalls 3:close 9:mmap --nothink
```

The client uses the included graphs, function-source corpus, and `syscall_profiles/<id>:<name>/nginx-ltp-redis` seeds. It writes expanded graphs, function lists, and query logs under `syscall_procs/`. Run it for all syscalls needed by your workload. `parse-cfg.py` rebuilds the full graph from supplied JSON; regenerating the complete static-analysis inputs for another kernel requires additional preprocessing.

To use fresh profiles, merge each workload's `*-all.txt` files with `syscall-merge-diff-prog.py -i nginx ltp redis-server -o nginx-ltp-redis` and synchronize them into `guest/kern-cfg/syscall_profiles/`. Convert inference results back for the logger:

```bash
cd "$APPARE/guest/kern-cfg"
rsync -a syscall_procs/ ../vmhome/kftracer/syscall_procs/
cd "$APPARE/guest/vmhome/kftracer"
python3 proc-llm-results.py
python3 ud2.py -p nginx -i result_nothink --use-llm
```

`ud2.py` also requires matching guest `kallsyms`, `kobjdump` (`objdump -d` of the guest `vmlinux`), and `syscall_profiles/syscalls.csv`. Here `-i` selects the reference filename inside each syscall directory. For dynamic-only profiles, use `-i nginx-all.txt` without `--use-llm`. Outputs go to `out_UD2/<program>/`; maps for Nginx, Redis, and Memcached are included.

## Logger

With the guest stopped, compile the selected map into KVM:

```bash
cd "$APPARE/host-os"
python3 kvm_cf_template_gen.py nginx # replace this with your profiles
sudo ./rebuild-kvm.sh
```

This generates `kvm/mmu/template.h`. Reload KVM after host reboots or policy changes. If decoding traces, prepare the parser's metadata below before this rebuild and the capture session.

Boot the guest, then initialize logging **inside it**:

```bash
cd ~
./run_deeplog.sh cf
# Run the target workload, then request statistics:
./users/statistics
```

Check host `sudo dmesg` for initialization and statistics. PT logs are written to **`/var/log/pt.log.<vcpu-id>`** and truncated when the monitor is reinitialized. Disable logging with `sudo rmmod cf_logging_enable`; re-enable it with `sudo insmod ~/users/cf_logging_enable.ko` after context initialization.

Target process names are compiled into [`is_tracked_proc()`](guest/kernel/linux/arch/x86/entry/common.c). Kernel changes require updating profiles/maps and hard-coded addresses such as `vmcall_addr` and `white_function_pages` in `host-os/kvm/mmu/libept.c`. Changing VM memory/vCPU counts also requires reviewing `users/walk_memory.c` and `VCPU_MAX`.

## Parser

First configure [`host-os/vmlinux_code_loader.py`](host-os/vmlinux_code_loader.py) for your guest `vmlinux` and address layout; its default points to an external CVE kernel. Install its dependencies (`angr`, `pygraphviz`, `networkx`, `ipython`, and system Graphviz development libraries), then run it. It generates `outputs/vmlinux_code.metadata`, `outputs/vmlinux_code.vmem<N>`, and `kvm/mmu/metadata.h`.

Rebuild KVM with that metadata before capture. Refresh the code images after guest boot using `kvm_hypercall3(0x20009, 0, 0, 0)` from a guest helper including `users/tests.h`. The decoder needs these runtime code images and PT logs from the same session.

Build the standalone decoder on the analysis machine:

```bash
cd "$APPARE/host-os/pt-tools/pt"
make -C distorm/make/linux clean
make -C distorm/make/linux
make clean
make
./driver-csv \
  --input-ptrace /path/to/pt.log.0 \
  --input-vmlinux-vmem "$APPARE/host-os/outputs/vmlinux_code.vmem" \
  --input-vmlinux-metadata "$APPARE/host-os/outputs/vmlinux_code.metadata" \
  --output-file /path/to/trace.csv
```

The `.vmem` argument is a **prefix**, not a numbered file. Repeat for each vCPU log. The CSV exporter recovers code addresses and syscall fields; it does not automatically generate the paper's complete merged forensic graph.

## Evaluations and scope

Run servers in the guest and clients on the host. See the workload notes for [Nginx](server-evals/nginx/README.md), [Redis](server-evals/redis/README.md), [Memcached](server-evals/memcached/README.md), and [Phoronix](server-evals/phoronix/README.md). Each network workload has a `benchmark.sh` targeting the forwarded port. Auditd comparison scripts configure separate audit rules; APPARE is enabled through `cf_logging_enable.ko`.

NOTE: The prototype uses syscall-end/context-switch stopping instead of the paper's proposed guard stack and exempts interrupt/common code from selective tracing (also clarified in the ppaer's Implementation). 

Its scope is kernel control-flow hijacking; a logged divergence can also be benign but uncommon behavior. Profiles and code images must match the exact guest kernel build.