# REFACTOR_PLAN.md

> 本文件记录 TinyWebServer 全面重构的审计结论、改造计划、优先级、假设与兼容性风险。
> 审计基线：commit `d5e7e4c`（master 最新）。审计方式：通读全部源码（22 个 .cpp/.hpp）、构建文件、CI、文档。

## 1. 当前架构

```
main.cpp
  └─ WebServer（主线程 = Reactor 事件循环）
       ├─ Epoller        epoll_create1/add/mod/del/wait 封装
       ├─ TimerHeap      最小堆 + unordered_map(refMap)；peek() 决定 epoll_wait 超时
       ├─ ThreadPool     固定线程池（2x 核数）+ BlockingQueue<std::function<void()>>
       ├─ connections_   unordered_map<int, HttpConnection>（键 = fd）
       └─ AsyncLogging   后台线程，双缓冲，每 3s 刷盘
  └─ 可选 SqlConnPool   惰性单例（仅 POST /login.html、/register.html 触发），monitor 线程 60s 健康检查/收缩
```

请求链路：

1. `accept4` → `addClient`：`connections_[fd].init()` + `timer_->addTimer(fd, timeoutMS_, closeConn)` + `addFd(fd, EPOLLIN|EPOLLONESHOT|EPOLLRDHUP)`
2. `EPOLLIN` → `dealRead` → 线程池 `onRead`：`readv` 入 `Buffer` → `process()`：`HttpRequest::parse`（FSM：请求行→Header→Body）→ `HttpResponse::makeResponse`（mmap 静态文件）→ 设 `iov_[2]`（头 + mmap 文件）
3. `modFd(EPOLLOUT)` → `dealWrite` → 线程池 `onWrite`：`writev` 循环写出；写尽且 keep-alive → 恢复 `EPOLLIN`
4. 每次读/写事件 `extentTime` 重置连接定时器；定时器到期在主线程执行 `closeConn`

## 2. 风险清单

### P0（崩溃 / 内存安全 / 数据丢失 / 高危安全）

| # | 位置 | 问题 |
|---|------|------|
| P0-1 | `HttpResponse::makeResponse/addContent` | **目录遍历**：`srcDir_ + path_` 直接 `stat/open/mmap`。`GET /../../etc/passwd`、绝对路径、URL 编码 `..`（`%2e%2e`）均可逃逸文档根读取任意文件 |
| P0-2 | `WebServer::onWrite` | **响应数据丢失**：`write()` 循环在剩余 ≤10240 字节时退出且 `ret>0`，`onWrite` 不满足任何分支 → `closeConn`，≤10KB 尾部数据被丢弃 |
| P0-3 | `WebServer` 主线程 / 线程池 worker | **连接 UAF/数据竞争**：worker 执行 `onRead/onWrite`（访问 `Buffer`、`iov_`、mmap 文件）期间，主线程定时器回调/`EPOLLERR` 可同时 `closeConn` → 并发 `unmapFile/close/read/write`；`EPOLLONESHOT` 只防事件并发，防不了定时器并发 |
| P0-4 | `WebServer::start()` | `assert(connections_.contains(fd))` 在 `fd == listenFd_` 判断**之前**执行：Debug 构建每来一个新连接即断言失败崩溃；Release（NDEBUG）下 `EPOLLERR` 等对未知 fd 会插入默认 `HttpConnection` 脏对象 |
| P0-5 | `TimerHeap` + `WebServer::closeConn` | 关闭连接**从不删除定时器**（`timerId_` 声明未用，`addTimer` 以 fd 为 id）；fd 被内核复用后旧定时器到点 → **误杀新连接**；定时器无限堆积 |
| P0-6 | `ThreadPool` | 空队列竞态下 worker 可阻塞在 `BlockingQueue::pop()` 的 `cvConsumer` 上，`shutdown()` 只 `notify cv_` → **shutdown 挂死**；`ThreadPool` 无析构（默认析构 joinable 线程 → `std::terminate`）；`WebServer` 无任何优雅退出路径 |
| P0-7 | `HttpRequest::parseFromUrlEncoded` | **越界读**：`%` 位于字符串末尾时访问 `content_[i+1]/content_[i+2]`（UB）；且解码逻辑错误（将十六进制值改写为十进制数字字符，未真正解码，`%20` 变成 `"32"`） |
| P0-8 | `LogStream::intToDecStr` | 负数时 `tmp % 10` 为负 → `number[lsd]` **负数下标越界读**（UB）；记录负数（如超时差、错误码）即触发 |

### P1（正确性 / 资源 / 协议语义）

| # | 位置 | 问题 |
|---|------|------|
| P1-1 | `HttpRequest::parse` | 请求行/Header 不完整时误报 400 并关闭连接（非增量等待）；`Content-Length` 未校验未使用；chunked 编码未识别（按一行 body 解析 → 协议错乱）；HEAD 请求仍发送 body；无 URI/Header 长度与数量上限；pipeline 支持不完整 |
| P1-2 | `Buffer::readFd` | 读缓冲无上限：恶意客户端持续发送 → 内存无限增长；无请求体大小限制 |
| P1-3 | `WebServer` 析构顺序 | `Logger::outPutCallback_` 静态回调捕获 `WebServer*`；`asyncLogging_` 是最后成员（最先析构），之后任何线程（SqlConnPool monitor、线程池）再写日志 → **悬垂 this UAF**；`SqlConnPool` monitor 线程 `sleep(60s)` 无法中断，析构 join 阻塞最多 60s |
| P1-4 | `WebServer::start()` 事件处理 | `EPOLLERR` 分支 `closeConn` 后同一事件继续走 `EPOLLIN/EPOLLOUT` 分支，对已关闭连接再读/写（脏操作，虽被 `isClose_` 部分保护） |
| P1-5 | `HttpConnection` | `inline static mutex_` 全局一把锁保护 `connectionCount`（所有连接共享、锁内打日志）；`isKeepAlive` 只精确匹配 `"keep-alive"/"Keep-Alive"`，HTTP/1.1 无 Connection 头默认返回 false（与 RFC 相反）；Header 值大小写/空白未归一化 |
| P1-6 | `Config` | 无校验：`WebServer` 构造 `assert(sitePath)` 在 Release 下失效；类型错误/缺键抛未捕获异常 → `terminate`；端口、超时无范围检查；`_get` 每次深拷贝整个 YAML |
| P1-7 | `SqlConnPool` | 每次 login/register 都执行 `CREATE TABLE IF NOT EXISTS`（DB 往返浪费）；密码明文存储比较（无哈希）；DB 未就绪时惰性初始化抛异常 → `terminate`；`releaseConnection` 对不在使用中的连接直接丢弃（泄漏） |
| P1-8 | `Logger` 静态状态 | `lowestLevel_/colorful_/outPutCallback_` 非原子、无同步（TSan 必报） |
| P1-9 | `TimerHeap::tick` | 回调执行期间若修改堆（未来 closeConn 删定时器后），`pop()` 可能删错节点；需在回调后按 id 校验 |
| P1-10 | `Epoller::wait` | `int64_t` 超时强转 `int`：`timeoutMS_` 配置过大时截断（非致命但错误） |

### P2（工程化 / 可维护性 / 性能）

| # | 位置 | 问题 |
|---|------|------|
| P2-1 | CMake | 非 target-based（`file(GLOB)`）；`CMAKE_CXX_FLAGS` 从 `CMAKE_C_FLAGS` 派生（错误）；Debug 才开 `-Wall`；无 `-Wextra/-Wpedantic/-Wshadow`；无 CTest / sanitizer 选项 |
| P2-2 | CI | starter workflow：无测试（`ctest` 空转）、无格式检查、无 sanitizer |
| P2-3 | 工具链 | 无 `.clang-format`、`.clang-tidy` 配置 |
| P2-4 | 性能 | `extentTime` 每次读/写打 `LOGI`（高频日志）；`Buffer::retrieveAll` 全量 memset（64KB）；`Config::_get` 每次 `Clone` 深拷贝；`HttpRequest::parse` 每行构造临时 `std::string`；`addClient` 丢弃 timer id |
| P2-5 | 文档 | README 仅 Ubuntu 原生命令（无 WSL/Docker）；无 systemd 示例、无健康检查接口、无部署建议、无安全边界声明 |
| P2-6 | 其他 | 文件头版权块写 GPL 但 LICENSE 为 Apache-2.0（不一致）；`BlockingQueue::push(const T&)` 中 `std::move` 无效（误导）；`HttpRequest::getPost` 重复 `find` |

## 3. 改造计划（按序执行，每步保持可构建可运行）

### 阶段 0：开发环境（WSL2）
- 在 WSL2 Debian 中安装 cmake/ninja/pkg-config/mariadb-dev/gtest/clang 工具链。
- 新增 `scripts/build.sh`、`scripts/test.sh`、`scripts/dev_wsl.sh`（供 Windows 侧一键进入 WSL 构建）。
- 优先验证：原项目在 WSL 中可编译、可运行（作为重构基线）。

### 阶段 1：P0/P1 正确性与安全修复（先修，回归测试随改随加）
1. **HTTP 层重写**（P0-7、P1-1、P1-2）：
   - 增量解析：请求行/Header 不完整时返回"待更多数据"，不误报 400；
   - `Content-Length` 解析与校验（缺失时按无 body；POST 必须有合法长度）；超限 → 413；
   - chunked：显式检测并拒绝（501 Not Implemented），绝不按行拆 body；
   - URI/Header 行/Header 总数/请求体上限（配置化）；方法白名单 GET/POST/HEAD；
   - URL 解码：正确实现 `%XX` 与 `+`，非法编码返回 400；
   - 目录遍历防御（与 P0-1 联动）：解码 → 规范化（去 `..`/重复斜杠）→ 拒绝绝对路径与越界 `..` → 文件必须位于文档根内（`realpath` 前缀校验）。
2. **文件访问安全**（P0-1）：`HttpResponse` 改为接收已规范化且验证过的相对路径；open 前校验；`sendfile` 评估留到阶段 5。
3. **事件循环与写路径**（P0-2、P0-4、P1-4）：修 `onWrite`（有剩余数据一律 `modFd(EPOLLOUT)` 重臂）；修断言顺序；`EPOLLERR/EPOLLHUP` 统一走关闭且不再继续处理同一 fd 的其余事件。
4. **连接生命周期与定时器**（P0-3、P0-5、P1-9）：`closeConn` 级联删除定时器（按 timer id）；修复 `TimerHeap` 在回调中删除的安全；引入**每连接 in-flight 任务计数**：`closeConn` 只在主线程发起，设置 closing 标志并等待该连接的任务计数归零后再 close/unmap，消除 UAF 竞态。
5. **线程池与退出**（P0-6、P1-3）：`BlockingQueue`/`ThreadPool` 支持 shutdown 唤醒与排空；`ThreadPool` 析构调用 shutdown；信号处理（SIGINT/SIGTERM）→ 优雅退出：停 accept → 等待存量请求（超时上限）→ 关连接 → 停线程池 → 停日志；`Logger` 回调改为不捕获 `WebServer*`（日志单例或先解绑）；`SqlConnPool` monitor 可中断 sleep。
6. **杂项**（P0-8、P1-5、P1-8）：`LogStream` 负数修复（按无符号取模）；`isKeepAlive` 按 RFC（HTTP/1.1 默认 keep-alive，`Connection: close` 才关闭，值忽略大小写/空白）；Logger 静态状态加同步；`HttpConnection::connectionCount` 用原子计数。

### 阶段 2：架构重构（渐进，不推倒重写）
- 目录迁移为 `src/{app,net,http,concurrency,timer,log,config,storage,common}`（git mv 保留历史）；`main.cpp` 移入 `src/app`。
- `WebServer` 拆分为：`EventLoop`（epoll 循环）、`Acceptor`（监听/accept/连接数上限）、`ConnectionManager`（连接表 + 定时器协调），`WebServer` 仅做组装。
- 配置启动校验：全部配置项加载校验，错误给出明确、可定位的启动失败信息（退出码非 0）。
- 统一 RAII（fd 封装）与所有权（杜绝裸指针跨线程传递）。

### 阶段 3：工程化与测试
- CMake：target-based（`webserver_core` 静态库 + `webserver` 可执行 + `tests`），`option(BUILD_TESTING/ENABLE_SANITIZER)`，告警 `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2`（CI 加 `-Werror`，普通开发不加）；Debug/Release/RelWithDebInfo 均可用。
- `.clang-format`、`.clang-tidy` 配置并接入 CI 格式检查。
- 测试（GoogleTest，单一框架）：
  - 单元：Buffer 边界、HttpRequest（请求行/Header/Body、分段、畸形、URL 解码、路径穿越拦截、413）、MIME、TimerHeap（含回调删定时器）、Config（合法/非法）、ThreadPool（队列边界、shutdown、异常隔离）、RAII 生命周期；
  - 集成：起真实进程 + raw socket 客户端：健康检查、GET/HEAD、400/403/404/413、Keep-Alive、大响应部分写、慢客户端超时、并发、路径遍历、优雅退出（SIGTERM）。
  - 每个已修 P0 对应回归测试。
- Sanitizer：ASan/UBSan 在 CI 跑测试；TSan 提供选项（说明线程模型限制）。

### 阶段 4：文档与部署环境
- `docs/DEVELOPMENT_WINDOWS.md`（WSL2 从零到构建/测试/调试）、`Dockerfile`、`docker-compose.yml`（mariadb）、`scripts/*`、`config/config.example.yml`、systemd 单元示例、健康检查端点说明（`GET /healthz` 或最小替代）、安全边界文档、README 全面重写（Linux/WSL/Docker 命令为准）。

### 阶段 5：性能评估与针对性优化
- 压测脚本（wrk/hey/ab 均可），先测现状基线（QPS/延迟分位/错误率/CPU/内存），再逐项优化并复测对比；
- 候选优化（仅在有数据支撑时做）：`sendfile` 静态文件发送、Buffer 零拷贝/复用、高频日志降噪、`extentTime` 改 WARN 级、Config 只读一次。

## 4. 假设与兼容性风险

- **兼容性目标**：对外 HTTP 行为保持"静态文件 GET/HEAD、表单 POST 登录/注册"不变；`config.yml` 格式保持兼容（新增可选字段，缺省有默认值）；CLI 无参数。
- **破坏性变更**（将记录在 README 迁移说明）：
  1. 目录布局 `app/` → `src/`（构建产物路径不变，仍为 `build/`）；
  2. 原先"不完整请求行 → 立即 400 关闭"改为"等待更多数据，超时后按超时关闭"（行为更符合 HTTP/1.1）；
  3. 路径穿越请求由"可读任意文件"改为"403/404"（安全修复，属预期行为变更）；
  4. 无 `Connection` 头的 HTTP/1.1 请求由"关闭连接"改为"keep-alive"（RFC 正确行为）。
- **安全边界（不承诺）**：本项目不提供 TLS、认证授权、WAF、DDoS 防护；生产部署应由 Nginx/Caddy/Envoy 等网关承担 TLS 终止、限流与负载均衡。单进程仅承诺较高正确性/稳定性/可恢复性，**不承诺系统级高可用**（高可用需多实例 + 健康检查 + 故障转移）。
- **数据库**：登录/注册依赖外部 MariaDB；DB 不可用时**静态文件服务不受影响**（池为惰性初始化），但登录/注册返回错误页。密码存储不升级为哈希（超出当前范围，文档中声明）。
- **平台**：仅 Linux（含 WSL2/Docker）；不引入 Windows 代码路径。

## 5. 验证方式

- 每阶段结束：`scripts/build.sh`（Debug+Release）→ `ctest`（单元+集成）→ 手动 `curl` 冒烟；
- P0 修复即时回归：ASan/UBSan 构建跑全量测试；
- 阶段 5：压测前后对比数据写入 `docs/BENCHMARK.md`，无数据不写结论。

## 6. 执行状态（2026-08-16 更新）

- [x] 阶段 0：WSL2 Debian 13 工具链就绪；scripts/build.sh、test.sh、dev_wsl.sh
- [x] 阶段 1：全部 P0/P1 修复完成并经 ASan/UBSan 全量测试验证（详见 README 重构说明）
- [x] 阶段 2：目录迁移至 `src/`；WebServer 拆分为 Acceptor / ConnectionManager / 事件循环组装；
      配置启动校验（缺键/越界启动报错）
- [x] 阶段 3：target-based CMake（告警/测试/Sanitizer 选项）；clang-format/clang-tidy 配置；
      GoogleTest 单元测试 92 个 + 集成测试 27 项（CTest 注册）；CI 修复（构建+测试+格式+ASan）
- [x] 阶段 4：docs/DEVELOPMENT_WINDOWS.md、docs/DEPLOYMENT.md（systemd/内核/安全边界）、
      Dockerfile、docker-compose.yml（mariadb）、config/config.example.yml、README 重写
- [x] 阶段 5：基准实测（WSL2 环境 6.6k-7.2k QPS，0 错误）写入 docs/BENCHMARK.md；未做无依据优化

### 遗留事项（超出本次范围，已文档化）

- 密码存储未升级为哈希（演示功能；真实用户系统需自行实现）
- sendfile / 多 Reactor 改造：需裸机 Linux 复测后再决策
- TSan 未在 CI 启用（WSL 环境不可靠；提供 `-DENABLE_TSAN=ON` 选项供裸机使用）

### 补充交付（2026-08-16 二次审计）

- 配置化补全（目标第 8 条）：`server.host`（监听地址）、`server.threads`（线程数）、
  `server.max_connections`（连接上限）、`server.max_request_size`/`server.max_body_size`
  （请求限制）；可选键缺失用内置默认值，键存在但类型错误仍启动报错（`ConfigKeyMissing` 区分）
- 健康检查端点：`GET /healthz` -> `200 ok`（纯文本，不访问文件系统；HEAD 亦支持）
- 集成测试补 3 项：healthz（GET/HEAD）、慢客户端（半请求行/半 Header 超时关闭）
- 单元测试补 3 项：Config 可选键默认值/类型错误、HttpResponse 文本响应（含对象复用）
- CMake 增加 `ENABLE_TSAN` 选项（与 ASan 互斥）
