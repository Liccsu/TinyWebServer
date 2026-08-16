#!/usr/bin/env bash
# Windows + WSL2 一键构建脚本：在 WSL 内完成配置、编译、测试。
# 用法（Windows PowerShell / Git Bash）:
#   wsl -d Debian -- bash -lc 'cd /mnt/d/workspace/AgentWorkflow/TinyWebServer && ./scripts/dev_wsl.sh'
set -euo pipefail

echo "== WSL 开发构建 =="
echo "  发行版: $(. /etc/os-release && echo "$PRETTY_NAME")"
echo "  编译器: $(g++ --version | head -1)"
echo "  CMake:  $(cmake --version | head -1)"

# 源码位于 Windows 挂载目录时，构建产物放 Linux 文件系统可避免 9p 性能问题；
# 默认放仓库内 build/（跨会话保留）。
BUILD_DIR="${BUILD_DIR:-build}"
echo "== 构建目录: $BUILD_DIR =="

./scripts/build.sh Debug "$BUILD_DIR"
./scripts/test.sh "$BUILD_DIR"

echo
echo "== 全部完成。运行: =="
echo "  cd $BUILD_DIR && ./webserver"
