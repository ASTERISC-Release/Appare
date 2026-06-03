#!/bin/bash -e

# Get environment variables
source .env

# Initial information
echo "-----------------------------------------------------------"
echo "Function: This script SSHs into an already running virtual machine"
echo ""
echo "  1. Ensure the script '.ssh/setup-ssh.sh' was executed before"
echo "  2. Ensure the script 'start-vm.sh' was executed before"
echo "  3. Execute 'exit' to stop the SSH session"
echo "-----------------------------------------------------------"
echo ""

# SSH onto the vm
ssh $USERNAME@localhost -p 8000 -i .ssh/deeplog-key

