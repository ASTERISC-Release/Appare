#!/usr/bin/env bash
# Usage: ./benchmark-nginx.sh <output-dir> <url>
# Example:
#   ./benchmark-nginx.sh out-nginx "http://127.0.0.1:9000/10K.html"

if [ $# -ne 1 ]; then
  echo "Usage: $0 <output-dir>"
  exit 1
fi

OUTDIR=$1
#URL="http://127.0.0.1:9000/index.html"
URL="http://127.0.0.1:9000/index.nginx-debian.html"
REQUESTS=${REQUESTS:-10000}    # ab -n
RUNS=${RUNS:-1}                # 重复次数
clients=(1 2 4 8 16 24 32)      # 并发点列表

mkdir -p "$OUTDIR"
rm -f "$OUTDIR/run"*.csv

for RUN in $(seq 1 $RUNS); do
  for IDX in "${!clients[@]}"; do
    C=${clients[$IDX]}
    for ATTEMPT in $(seq 1 10); do
      echo "[RUN $RUN][IDX $IDX] concurrency=$C"

      # 1) 执行 ab
      TMP="$OUTDIR/run${RUN}_idx${IDX}.tmp"
      ab -n $REQUESTS -c $C -k "$URL" > "$TMP" 2>&1

      # 2) 确保输出里有吞吐行
      if ! grep -q "Requests per second" "$TMP"; then
        continue
      fi

      # 3) 解析 Requests per second
      RPS=$(awk '/Requests per second/ {print $4}' "$TMP")

      # 4) 解析第一个 Time per request (mean)，排除 “across all concurrent”
      LAT=$(awk '/Time per request/ && !/across all concurrent/ {print $4; exit}' "$TMP")

      # 5) 写 CSV：concurrency,rps,latency(ms)
      echo "$C,$RPS,$LAT" > "$OUTDIR/run${RUN}_idx${IDX}.csv"
      rm "$TMP"
      break
    done
  done
done
