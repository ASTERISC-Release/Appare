#!/bin/bash -e

# Empty log file(s)
sudo dd if=/dev/zero of=/var/log/audit/audit.log bs=1 count=1
sudo dd if=/dev/zero of=/var/log/audit/audit.log.1 bs=1 count=1
sudo dd if=/dev/zero of=/var/log/audit/audit.log.2 bs=1 count=1
sudo dd if=/dev/zero of=/var/log/audit/audit.log.3 bs=1 count=1
sudo dd if=/dev/zero of=/var/log/audit/audit.log.4 bs=1 count=1

# Run benchmark
./run-test.sh pts/compress-7zip 7zip

# Print results
cat log_7zip | grep "Average:"
cat log_7zip | grep "Time:"