#!/bin/bash
 
if [ $# -ne 1 ];
then
    echo "exit..."
    exit 1
fi
 
SESSION_NAME=$1
 
echo "tmux a -t ${SESSION_NAME}"
 
echo "${SESSION_NAME}" > ~/.tmp_tmux_name
tmux new-session -s ${SESSION_NAME} -d -n "${SESSION_NAME}" "bash"
tmux send -t ${SESSION_NAME}.0 ". ${HOME}/.bash_profile" ENTER
