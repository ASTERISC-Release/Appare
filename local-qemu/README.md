# Local QEMU Installation

Due to a bug in exisiting QEMU releases, Intel PT is not supported within the guest VM. In order to fix this a simple patch is required. This directory houses and installs that simple patch!

## Prereqs

The following packages are required for installing qemu locally!

```bash
sudo apt install libglib2.0-dev libgcrypt20-dev zlib1g-dev autoconf automake libtool bison flex libpixman-1-dev ninja-build
```

## Installation

The installation script `install-qemu-pt.sh` is all you need to run in order to set up our local QEMU with pt support!

