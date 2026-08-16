#!/usr/bin/env bash
# 构建脚本：配置 + 编译 TinyWebServer（Linux/WSL/Docker 环境）。
# 用法: ./scripts/build.sh [Debug|Release|RelWithDebInfo] [构建目录]
set -euo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="${2:-build}"
EXTRA_ARGS=("${@:3}")

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

# 跳过测试构建的开关：CI 全量构建时保留测试
if [[ -n "${SKIP_TESTS:-}" ]]; then
    EXTRA_ARGS+=("-DBUILD_TESTING=OFF")
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    "${EXTRA_ARGS[@]}"

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo
echo "构建完成: $BUILD_DIR/$([ -n "$(command -v uname)" ] && echo webserver)"
ls -la "$BUILD_DIR/webserver"
