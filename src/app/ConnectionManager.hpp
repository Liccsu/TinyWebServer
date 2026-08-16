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

#ifndef TINYWEBSERVER_CONNECTIONMANAGER_HPP
#define TINYWEBSERVER_CONNECTIONMANAGER_HPP

#include <atomic>
#include <cstdint>
#include <mutex>
#include <netinet/in.h>
#include <queue>
#include <unordered_map>

#include "../http/HttpConnection.hpp"
#include "../net/Epoller.hpp"
#include "../timer/TimerHeap.hpp"

class ThreadPool;

// 连接生命周期管理（仅主线程持有/调用）：
//   - 连接表与 fd -> timerId 映射
//   - 连接关闭（含定时器级联删除、在途任务等待）
//   - worker -> 主线程关闭请求通道（eventfd 唤醒 + 互斥队列）
//   - 读/写事件的任务提交（worker 执行网络 I/O 与 HTTP 处理）
// 线程协作契约见 WebServer 顶部注释。
class ConnectionManager {
    Epoller& epoller_;
    TimerHeap& timer_;
    ThreadPool& threadPool_;
    int timeoutMS_;

    // worker -> 主线程的关闭请求通道：
    // eventfd 仅作为唤醒信号（计数器语义，不能承载数据）；
    // 实际请求 (fd, generation) 存于互斥队列
    int wakeupFd_{-1};
    std::mutex closeReqMutex_;
    std::queue<std::pair<int, uint64_t>> closeReqs_;

    std::unordered_map<int, HttpConnection> connections_;
    // fd -> 定时器 id（关闭连接时级联删除定时器）
    std::unordered_map<int, uint64_t> timerId_;

    // 性能诊断计数器（cleanup 时输出）
    mutable std::atomic<long long> statAccept_{0};
    mutable std::atomic<long long> statReadSubmit_{0};
    mutable std::atomic<long long> statWriteSubmit_{0};
    mutable std::atomic<long long> statCloseConn_{0};

    void addClient(int fd, const sockaddr_in& addr);

    void requestClose(int fd, uint64_t generation);

    void extentTime(const HttpConnection* client) const;

public:
    ConnectionManager(Epoller& epoller, TimerHeap& timer, ThreadPool& threadPool, int timeoutMS);

    ~ConnectionManager();

    ConnectionManager(const ConnectionManager&) = delete;

    ConnectionManager& operator=(const ConnectionManager&) = delete;

    [[nodiscard]]
    int wakeupFd() const {
        return wakeupFd_;
    }

    // accept 一个新连接并登记（定时器 + epoll 注册）
    void acceptConnection(int fd, const sockaddr_in& addr);

    // 提交读事件处理任务（worker 线程执行 read + process）
    void dealRead(HttpConnection* client);

    // 提交写事件处理任务（worker 线程执行 write）
    void dealWrite(HttpConnection* client);

    // 仅主线程调用：等待在途任务结束后释放连接资源
    void closeConn(HttpConnection* client);

    // 处理 eventfd 中的关闭请求
    void dealWakeup();

    // 事件循环内按 fd 查找连接；不存在返回 nullptr
    [[nodiscard]]
    HttpConnection* find(int fd);

    // 关闭全部连接并清空定时器（优雅退出收尾）
    void closeAll();

    void printStats() const;
};

#endif // TINYWEBSERVER_CONNECTIONMANAGER_HPP
