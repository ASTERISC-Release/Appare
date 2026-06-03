#!/bin/bash -e

# Download Phoronix
wget https://phoronix-test-suite.com/releases/repo/pts.debian/files/phoronix-test-suite_10.8.4_all.deb

# Phoronix is known to fail since it will need some packeges
sudo dpkg -i phoronix-test-suite_10.8.4_all.deb || true 

# Let apt install these packages automatically
sudo apt install -f