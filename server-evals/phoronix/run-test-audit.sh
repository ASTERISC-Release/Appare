#!/bin/bash -e

# This is for hardlog execution
ARCH=`uname -m`
RUN_TIMES=1
HARDLOG_GID=9999
PROCS=[]
SYSCALL_CMD="-S read -S readv -S write -S writev -S sendto -S recvfrom -S sendmsg -S recvmsg -S mmap -S mprotect -S link -S symlink -S clone -S fork -S vfork -S execve -S open -S close -S creat -S openat -S mknodat -S mknod -S dup -S dup2 -S dup3 -S bind -S accept -S accept4 -S connect -S rename -S setuid -S setreuid -S setresuid -S chmod -S fchmod -S pipe -S pipe2 -S truncate -S ftruncate -S sendfile -S unlink -S unlinkat -S socketpair -S splice"

stop_logger() {
    # clear all auditctl commands
    sudo auditctl -D

    # stop the logging process
    sudo auditctl -e 0
}

start_logger() {
    # clear all auditctl commands
    sudo auditctl -D
    
    # exclude useless commands
    sudo auditctl -a never,exclude -F msgtype=SERVICE_START
    sudo auditctl -a never,exclude -F msgtype=SERVICE_STOP
    sudo auditctl -a never,exclude -F msgtype=PROCTITLE
    sudo auditctl -a never,exclude -F msgtype=USER_END
    sudo auditctl -a never,exclude -F msgtype=USER_START
    sudo auditctl -a never,exclude -F msgtype=CRED_REFR

    # enable auditing
    sudo auditctl -e 1
}

# clear and start the log
sudo rm -rf /var/log/audit/audit.log.*
start_logger

export FORCE_TIMES_TO_RUN=5

# add an auditctl command and make backlog buffer huge
sudo auditctl -a always,exit -F arch=$ARCH -F gid=$HARDLOG_GID $SYSCALL_CMD -b 100000 --backlog_wait_time 20
echo "detail: added the following logging rules"
echo "sudo auditctl -a always,exit -F arch=$ARCH -F gid=$HARDLOG_GID $SYSCALL_CMD -b 100000 --backlog_wait_time 20"

sleep 2

echo "detail: starting the benchmark: $1 and saving results in log_$2"

# run asked test
echo "sg seclog_grp \"time phoronix-test-suite run $1\""
sg seclog_grp "echo \"n\" | phoronix-test-suite run $1" | tee log_$2

sleep 2

stop_logger