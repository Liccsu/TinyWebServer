# 性能基准报告

> 数据为真实运行结果。测试环境为 **WSL2（软件虚拟化网络栈）**，不代表裸机 Linux 性能。
> 复现方法见文末；请不要将 WSL 数据当作生产环境指标。

## 测试环境

| 项 | 值 |
|---|---|
| 主机 OS | Windows 11 Pro（build 26200） |
| 测试环境 | WSL2 Debian 13 (trixie)，内核为 WSL 虚拟化 |
| CPU | AMD Ryzen 7 9850X3D（WSL 可见 16 核） |
| 编译器 | GCC 14.2.0，`-O3 -DNDEBUG -Wall -Wextra -Wpedantic -Wshadow -Wformat=2` |
| 服务器 | `build-rel/webserver`（Release），单 Reactor 主线程 + 32 worker 线程 |
| 压测工具 | wrk（keep-alive） |
| 静态站点 | 仓库 `dist/`（Windows 挂载 9p 文件系统） |

## 结果

### index.html（7.5 KB）

| 并发 | 线程 | QPS | 传输 | p50 | p99 | 错误 |
|---|---|---|---|---|---|---|
| 100 | 4 | **7232** | 54.1 MB/s | 13.4 ms | 18.7 ms | 0 |
| 500 | 4 | **6611** | 49.5 MB/s | 71.9 ms | 146.8 ms | 0 |

### favicon.ico（小文件）

| 并发 | 线程 | QPS | 传输 | p50 | p99 | 错误 |
|---|---|---|---|---|---|---|
| 1000 | 4 | **6632** | 241.4 MB/s | 145.0 ms | 190.6 ms | 0 |

## 观察

- 100 并发下 QPS ~7.2k、p99 < 20ms；并发增大到 500-1000 时 QPS 稳定在 ~6.6k，
  延迟线性上升（单事件循环 + WSL 网络栈的排队效应），**无错误、无连接丢失**。
- 瓶颈分析（未做进一步优化前的判断）：
  1. WSL2 localhost 网络栈（NAT 转发）——换裸机 Linux 预期显著提升；
  2. 静态文件经 `mmap` 提供（`dist/` 位于 9p 挂载，文件 I/O 慢于本地盘）；
  3. 单 Reactor 主线程串行分发（当前设计，32 worker 只做 I/O 与解析）。
- 未做猜测式优化：上述数据不足以支撑 sendfile/多 reactor 改造的结论，
  如需进一步优化，应在裸机 Linux 上复测后再决策。

## 复现

```bash
# 1. 构建 Release
./scripts/build.sh Release build-rel

# 2. 启动服务器（工作目录含 config/config.yml 与 dist/）
cd <工作目录> && ulimit -n 65535 && ../build-rel/webserver &

# 3. 压测
./scripts/benchmark.sh http://127.0.0.1:6666/index.html 10 100 4

# 压测后优雅退出验证
kill -TERM <pid>; echo $?   # 期望 0（日志出现 "Server stopped"）
```
