#!/usr/bin/env bash
# 压测脚本：wrk 对本地服务器做可复现基准。
# 用法: ./scripts/benchmark.sh [url] [duration] [connections] [threads]
set -euo pipefail

URL="${1:-http://127.0.0.1:6666/index.html}"
DURATION="${2:-10}"
CONNECTIONS="${3:-100}"
THREADS="${4:-4}"

if ! command -v wrk >/dev/null 2>&1; then
    echo "缺少 wrk：sudo apt install -y wrk" >&2
    exit 1
fi

echo "== 环境 =="
echo "  $(. /etc/os-release && echo "$PRETTY_NAME")"
echo "  CPU: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | xargs) x $(nproc)"
echo "  ulimit -n: $(ulimit -n)"
echo "== 基准 =="
echo "  URL: $URL"
echo "  时长: ${DURATION}s  并发: $CONNECTIONS  线程: $THREADS"
echo

wrk -t"$THREADS" -c"$CONNECTIONS" -d"${DURATION}s" --latency "$URL"
