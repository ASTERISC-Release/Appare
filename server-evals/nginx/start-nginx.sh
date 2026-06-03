#!/bin/bash -e

# Kill all current instances of NGINX
sudo killall -9 nginx || true

# Stop and clear all audit commands
stop_logger() {
    # clear all auditctl commands
    sudo auditctl -D

    # stop the logging process
    sudo auditctl -e 0
}

stop_logger

echo "detail: starting memcached NATIVELY"

# Run NGINX under the special group
sudo nginx &