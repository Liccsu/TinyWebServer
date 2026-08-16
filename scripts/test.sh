#!/usr/bin/env bash
# 测试脚本：运行全部 CTest（单元 + 集成）。
# 用法: ./scripts/test.sh [构建目录]
set -euo pipefail

BUILD_DIR="${1:-build}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "构建目录不存在: $BUILD_DIR （先运行 ./scripts/build.sh）" >&2
    exit 1
fi

cd "$BUILD_DIR"
ctest --output-on-failure -C Debug
