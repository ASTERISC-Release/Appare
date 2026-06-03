# log the output of tmux
TMUX_NAME=`cat ~/.tmp_tmux_name` 2> /dev/null
 
 
if [[ $TERM = "screen" ]] && [[ $(ps -p $PPID -o comm=) = "tmux" ]]; then
mkdir /home/logs 2> /dev/null
logname="$(date '+%d%m%Y%H%M%S').tmux_${TMUX_NAME}.log"
script -f /home/logs/${logname}
exit
fi
