# TinyWebServer

轻量级、高性能的单机静态 Web Server，C++20 编写，仅支持 Linux。

基于 **epoll + 线程池的 Reactor 模式**，支持 HTTP/1.0 与 HTTP/1.1（GET / POST / HEAD），
提供静态文件服务、表单登录/注册（MariaDB + 预处理语句）、高性能异步日志、连接超时管理。
构建/测试/运行均在 **Linux / WSL2 / Docker** 环境中进行。

> **安全边界**：这是一个静态文件 HTTP 服务器，不是 WAF。TLS 终止、认证授权、限流与 DDoS
> 防护请由 Nginx / Caddy / Envoy 等网关承担（见 [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md)）。
> 单进程只承诺较高的正确性、稳定性与可恢复性，**不承诺系统级高可用**（高可用需多实例 +
> 反向代理 + 健康检查 + 故障转移）。

## 功能特性

- Reactor 事件驱动：单线程 epoll 事件循环 + 工作线程池（网络 I/O 与 HTTP 处理）
- HTTP/1.1：增量解析、Keep-Alive、`Content-Length` 校验、合理状态码
  （400/403/404/405/413/414/500/501）、HEAD 请求
- 静态文件：`mmap` 传输、MIME 类型表、目录默认文档（`/index.html`）
- 安全：目录遍历防御（URL 编码/符号链接/绝对路径多重拦截）、请求体与 Header 大小上限、
  防 CRLF 注入、pipeline 显式拒绝、错误响应不泄露内部信息
- 异步日志：双缓冲、后台线程落盘、按天/大小滚动、日志级别过滤、背压丢弃保护
- 连接管理：最小堆定时器（空闲超时）、`EPOLLONESHOT` + 在途任务计数保证无 UAF 竞态、
  `eventfd` 关闭通道 + fd 代际校验防复用误杀
- 优雅退出：SIGINT/SIGTERM -> 停止 accept -> 等待存量请求 -> 回收线程与资源
- 配置：YAML（`config/config.yml`，首次运行自动生成，缺键启动报错）

## 目录结构

```text
.
├── src/
│   ├── app/           # 服务组装（WebServer）、连接管理（ConnectionManager）、main
│   ├── net/           # Epoller、Acceptor、Buffer
│   ├── http/          # HttpConnection、HttpRequest、HttpResponse
│   ├── concurrency/   # ThreadPool、BlockingQueue
│   ├── timer/         # TimerHeap（最小堆定时器）
│   ├── log/           # Logger、AsyncLogging、LogFile、LogStream、FixedBuffer
│   ├── config/        # Config（yaml-cpp）
│   └── storage/       # SqlConnPool（MariaDB 连接池，RAII）
├── tests/
│   ├── unit/          # GoogleTest 单元测试（92 个用例）
│   └── (集成测试)      # scripts/run_integration_tests.py（27 项，注册于 CTest）
├── docs/              # 开发/部署/基准文档
├── scripts/           # 构建、测试、压测、WSL 开发脚本
├── config/            # 配置示例
└── dist/              # 静态网站根目录
```

## 快速开始（WSL2 / Docker）

### 方式一：WSL2（推荐开发）

```bash
# 依赖（Debian/Ubuntu）：cmake ninja pkg-config g++ libmariadb-dev* libgtest-dev
./scripts/dev_wsl.sh          # Debug 构建 + 全量测试

# 或 Release 构建
./scripts/build.sh Release

# 运行（首次运行生成 config/config.yml）
./build/webserver

# 验证
curl http://127.0.0.1:6666/
```

完整环境搭建见 [docs/DEVELOPMENT_WINDOWS.md](docs/DEVELOPMENT_WINDOWS.md)。

### 方式二：Docker

```bash
# 构建镜像（编译 + 运行两阶段）
docker build -t tinywebserver .

# 运行（登录/注册需要 MariaDB：docker compose up -d mariadb）
docker run -d -p 6666:6666 \
  -v $PWD/dist:/srv/tinywebserver/dist \
  -v $PWD/config:/srv/tinywebserver/config \
  --name tws tinywebserver

curl http://127.0.0.1:6666/
```

## 配置

首次运行在工作目录生成 `config/config.yml`（见 [config/config.example.yml](config/config.example.yml)）。
`server` 段必填键（port/timeout/site.path/log.*）缺一不可，缺键或非法值会在启动时给出明确错误并退出（非 0 退出码）；
可选键（host/threads/max_connections/max_request_size/max_body_size）缺失时使用内置默认值。

| 配置项 | 说明 | 默认 |
|---|---|---|
| `server.host` | 监听地址（IPv4，空串/0.0.0.0 = 所有接口） | 0.0.0.0 |
| `server.port` | 监听端口 | 6666 |
| `server.timeout` | 空闲连接/请求超时（毫秒） | 60000 |
| `server.threads` | 工作线程数（0 = 自动 2x CPU 核数） | 0 |
| `server.max_connections` | 最大并发连接数 | 65536 |
| `server.max_request_size` | 单请求读缓冲上限（字节） | 8 MiB |
| `server.max_body_size` | 请求体大小上限（字节） | 1 MiB |
| `site.path` | 静态网站根目录（启动时校验存在） | ./dist |
| `log.level` | 1=debug 2=info 3=warning 4=error 5=none | 2 |
| `log.output_to_file` | true 输出文件 / false 输出终端 | true |
| `mysql.*` | 登录/注册数据库（惰性初始化，静态服务不依赖） | - |

健康检查：`GET /healthz` 返回 `200 ok`（不访问文件系统），可用于负载均衡/编排探活。

## 测试

```bash
./scripts/test.sh              # CTest：单元（92）+ 集成（27）
ctest --output-on-failure      # 等价

# Sanitizer（ASan + UBSan）
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZER=ON
cmake --build build-asan -j $(nproc)
ASAN_OPTIONS=detect_leaks=1 ctest --output-on-failure --test-dir build-asan
```

CI（GitHub Actions）覆盖：Release 构建 + `-Werror` + 全量测试 + clang-format 检查 + ASan/UBSan 测试。

## 性能

WSL2 环境实测（Release，wrk keep-alive，详见 [docs/BENCHMARK.md](docs/BENCHMARK.md)）：

| 场景 | QPS | p99 | 错误 |
|---|---|---|---|
| 7.5 KB 页面，100 并发 | ~7.2k | 18.7 ms | 0 |
| 7.5 KB 页面，500 并发 | ~6.6k | 146.8 ms | 0 |
| 小文件，1000 并发 | ~6.6k | 190.6 ms | 0 |

> WSL2 为虚拟化网络栈；裸机 Linux 数据需自行复测，禁止将 WSL 数据作为生产指标。

## 部署

- systemd 服务单元、`ulimit -n`、内核参数建议：见 [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md)
- 反向代理（TLS 终止等）示例：见 [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md#8-反向代理示例nginx)

## 重构说明

本项目经全面审计与重构（[REFACTOR_PLAN.md](REFACTOR_PLAN.md)），主要变更：

- **修复**：目录遍历（含编码/符号链接）、URL 解码越界与错误解码、writev 尾部数据丢失、
  worker/主线程连接竞态（UAF）、定时器 fd 复用误杀、线程池 shutdown 死锁、
  LogStream 负数越界、日志回调解绑悬垂、eventfd 计数器误用等
- **重构**：`app/` 迁移至 `src/{app,net,http,concurrency,timer,log,config,storage}`；
  `WebServer` 拆分为 Acceptor / ConnectionManager / 事件循环组装；
  target-based CMake + 告警/测试/Sanitizer 选项
- **工程化**：GoogleTest 单元测试 + raw-socket 集成测试（CTest 注册）、
  clang-format/clang-tidy、CI 修复、WSL2/Docker 开发环境、部署文档

## 致谢

- [JehanRio/TinyWebServer](https://github.com/JehanRio/TinyWebServer)
- [chenshuo/muduo](https://github.com/chenshuo/muduo)
- 作者博客：[使用现代化C++编写WebServer项目总结](https://liccsu.com/archives/P1K8DUq2)

## License

Apache License 2.0（见 [LICENSE](LICENSE)）。
