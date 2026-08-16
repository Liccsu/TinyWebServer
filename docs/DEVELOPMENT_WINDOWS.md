# Windows + WSL2 开发环境指南

本项目的目标运行环境是 **Linux**，代码使用 Linux 系统调用（epoll、eventfd、非阻塞 socket 等），
**不支持 Windows 原生构建**。开发与验证请使用 WSL2 或 Docker。

## 1. 环境准备

### 1.1 安装 WSL2

```powershell
# 以管理员身份运行 PowerShell
wsl --install -d Debian
wsl --set-default -v 2
```

> 要求 WSL2（`wsl -l -v` 中 VERSION 列为 2）。若为 WSL1，执行
> `wsl --set-version <发行版名> 2` 升级。

### 1.2 安装构建依赖（WSL 内）

```bash
sudo apt update
sudo apt install -y \
  cmake ninja-build pkg-config g++ make git curl \
  libmariadb-dev libmariadb-dev-compat mariadb-server libmariadb3 \
  libgtest-dev clang-format clang-tidy valgrind gdb

# Debian 的 libgtest-dev 仅提供源码，需要编译安装一次
cd /usr/src/googletest
sudo cmake -B build -DCMAKE_BUILD_TYPE=Release
sudo cmake --build build -j $(nproc)
sudo cmake --install build
```

### 1.3 启动 MariaDB（登录/注册功能需要）

```bash
sudo systemctl enable --now mariadb
sudo mariadb -e "CREATE DATABASE IF NOT EXISTS tiny_web_server_db CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;"
```

> 纯静态文件服务不依赖数据库。数据库不可用时，登录/注册返回错误页，静态服务不受影响。

## 2. 源码位置与构建性能

仓库位于 Windows 挂载盘（如 `/mnt/d/...`），WSL 访问 9p 挂载目录较慢。
**建议**：把仓库复制到 WSL 文件系统（`~/TinyWebServer`）再构建：

```bash
cp -r /mnt/d/workspace/AgentWorkflow/TinyWebServer ~/TinyWebServer
cd ~/TinyWebServer
```

也可以直接在挂载目录构建（可用，但编译与启动更慢）。

## 3. 构建

```bash
# 一键脚本（Debug + 测试）
./scripts/dev_wsl.sh

# 或手动
./scripts/build.sh Release   # 构建目录 build/
./scripts/build.sh Debug     # 调试构建（含 -O0 -rdynamic + backtrace）

# 指定构建目录 / 跳过测试
BUILD_DIR=build-rel ./scripts/build.sh Release
SKIP_TESTS=1 ./scripts/build.sh Release build-slim
```

## 4. 运行

```bash
./build/webserver
# 首次运行在工作目录生成 config/config.yml，编辑后重启生效
```

验证：

```bash
curl http://127.0.0.1:6666/
curl -I http://127.0.0.1:6666/index.html
```

## 5. 测试

```bash
./scripts/test.sh              # 全部 CTest（单元 + 集成）
ctest --output-on-failure -C Debug --test-dir build
```

Sanitizer 构建（ASan + UBSan）：

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZER=ON
cmake --build build-asan -j $(nproc)
ASAN_OPTIONS=detect_leaks=1 ctest --output-on-failure --test-dir build-asan
```

## 6. 静态检查与格式

```bash
# 格式检查
find src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror
# 自动格式化
find src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# clang-tidy（需要 compile_commands.json）
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
clang-tidy -p build src/app/WebServer.cpp
```

## 7. 调试

### 7.1 GDB

```bash
gdb --args ./build/webserver
```

Debug 构建启用了 `HANDLE_BACKTRACE`：SIGSEGV/SIGABRT 时打印调用栈（需 `-rdynamic`）。

### 7.2 内存检查

```bash
# ASan/UBSan 构建见上；或使用 valgrind
valgrind --leak-check=full --track-origins=yes ./build/webserver
```

### 7.3 性能采样

```bash
# 压测（WSL 内安装 wrk / apache2-utils）
sudo apt install -y wrk
wrk -t4 -c100 -d10s http://127.0.0.1:6666/index.html
```

## 8. 常见问题

- **bind 失败（Address already in use）**：端口被占用，修改 `config/config.yml` 的 `server.port`。
- **打开文件数不足**：高并发压测前 `ulimit -n 65535`（见 `docs/DEPLOYMENT.md`）。
- **WSL 网络**：`127.0.0.1` 在 WSL 内直接可达；Windows 侧访问 WSL 服务同样使用
  `127.0.0.1`（WSL2 localhost 转发）。
- **文件权限**：/mnt/d 挂载文件默认 777，无需特殊处理。
