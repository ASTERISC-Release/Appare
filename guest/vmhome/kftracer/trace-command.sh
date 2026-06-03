#!/bin/bash

if [ $# -lt 2 ]; then
    echo "Usage: $0 <output-file.dat> <your-command> [args...]"
    exit 1
fi

OUTPUT="$1"
shift

CMD=("$@")

# 128 MB buffer
BUFSIZE_KB=262144

echo "Start tracing command: ${CMD[*]}"
echo "Output will be saved to $OUTPUT"

sudo trace-cmd record \
    -p function_graph \
    -e syscalls:sys_enter_* \
    -e syscalls:sys_exit_* \
    -b $BUFSIZE_KB \
    -O function_fork \
    -o "$OUTPUT" \
    -- "${CMD[@]}"

echo "Tracing finished."
