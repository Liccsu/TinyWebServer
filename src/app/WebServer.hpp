/*
 * Copyright (c) -2024
 * Liccsu
 * All rights reserved.
 *
 * This software is provided under the terms of the Apache License, Version 2.0.
 * Please refer to the accompanying LICENSE file for detailed information.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Apache License for more details.
 *
 * For further inquiries, please contact:
 * liccsu@163.com
 */

#ifndef TINYWEBSERVER_WEBSERVER_HPP
#define TINYWEBSERVER_WEBSERVER_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "../http/HttpConnection.hpp"
#include "../log/AsyncLogging.hpp"
#include "../net/Acceptor.hpp"
#include "../net/Epoller.hpp"
#include "../timer/TimerHeap.hpp"

class ConnectionManager;
class ThreadPool;

// 服务组装与主事件循环。
// 职责划分：
//   - Acceptor          监听 socket、accept4（src/net/Acceptor）
//   - ConnectionManager 连接表、定时器、关闭通道、读/写任务提交（src/app/ConnectionManager）
//   - ThreadPool        网络 I/O 与 HTTP 处理的工作线程
//   - TimerHeap         连接超时（主线程独占）
//   - AsyncLogging      异步日志
// 线程协作契约：
//   - 连接生命周期（accept/close/定时器/epoll 注册）只在主线程进行；
//   - worker 线程通过 beginTask/endTask 计数 + closing_ 标志与主线程的 closeConn 协作：
//     closeConn 等待在途任务结束后才释放 fd/mmap，杜绝 UAF 与数据竞争；
//   - worker 需要关闭连接时通过 eventfd 上报 (fd, generation)，主线程校验代际后执行关闭，
//     避免 fd 复用导致的误杀。
class WebServer {
    std::atomic<bool> isClose_{false};
    std::string sitePath_;
    int maxConnections_ = 65536;

    std::unique_ptr<Epoller> epoller_;
    std::unique_ptr<TimerHeap> timer_;
    std::unique_ptr<ThreadPool> threadPool_;
    std::unique_ptr<Acceptor> acceptor_;
    std::unique_ptr<ConnectionManager> connections_;

    AsyncLogging asyncLogging_;
    bool asyncLoggingStarted_ = false;

    // 单轮事件处理超过该毫秒数时告警（诊断事件循环停摆）
    static constexpr long long SLOW_LOOP_MS = 50;

    // 线程数：server.threads > 0 用配置值，否则 2x 硬件并发
    [[nodiscard]]
    static int configuredThreadCount();

    void dealListen();

    void cleanup();

public:
    WebServer();

    ~WebServer();

    // 运行事件循环；返回时所有资源已回收
    void start();

    // 优雅退出：停止接受新连接，等待存量请求完成（受连接超时约束），回收线程与日志
    void stop();
};

#endif // TINYWEBSERVER_WEBSERVER_HPP
