#!/bin/bash -e

export FORCE_TIMES_TO_RUN=5
echo "detail: starting the benchmark: $1 and saving results in log_$2"
echo \"n\" | phoronix-test-suite run $1 | tee log_$2