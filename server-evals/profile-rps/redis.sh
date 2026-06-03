#!/usr/bin/env bash
# Usage: ./benchmark-redis.sh <output-dir>

#   REDIS_HOST    (default 127.0.0.1)
#   REDIS_PORT    (default 11000)  # guest 6379
#   PIPELINE      (default 1)
#   REQUESTS      (default 50000)

if [ $# -ne 1 ]; then
  echo "Usage: $0 <output-dir>"
  exit 1
fi

OUTDIR=$1
HOST=${REDIS_HOST:-127.0.0.1}
PORT=${REDIS_PORT:-11000}
PIPELINE=${PIPELINE:-32}
REQUESTS=${REQUESTS:-100000}

RUNS=10

# 7 concurrency points
CONNS=(1 2 4 8 16 32 64)

mkdir -p "$OUTDIR"
rm -f "$OUTDIR/run"*.csv

for RUN in $(seq 1 $RUNS); do
  for IDX in "${!CONNS[@]}"; do
    C=${CONNS[$IDX]}

    # most attempt 10 times
    for ATTEMPT in $(seq 1 10); do
      echo "[RUN $RUN][IDX $IDX] clients=$C pipeline=$PIPELINE"

      redis-benchmark \
        -h "$HOST" -p "$PORT" \
        -c "$C" \
        -P "$PIPELINE" \
        -n "$REQUESTS" \
        -t get,set \
        --csv \
        > "$OUTDIR/run${RUN}_idx${IDX}.csv"

      # sanitize and check output
      if python3 check-redis.py "$OUTDIR/run${RUN}_idx${IDX}.csv"; then
        break
      fi
    done
  done
done
