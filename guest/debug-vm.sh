#!/bin/bash
gdb kernel/linux/vmlinux -ex "target remote:1234"
