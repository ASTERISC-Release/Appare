#!/bin/bash -e

stop_logger() {
    # clear all auditctl commands
    sudo auditctl -D

    # stop the logging process
    sudo auditctl -e 0
}

stop_logger