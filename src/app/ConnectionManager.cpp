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

#include "ConnectionManager.hpp"

#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>

#include "../concurrency/ThreadPool.hpp"
#include "../log/Logger.hpp"

ConnectionManager::ConnectionManager(Epoller& epoller, TimerHeap& timer, ThreadPool& threadPool, const int timeoutMS)
    : epoller_(epoller),
      timer_(timer),
      threadPool_(threadPool),
      timeoutMS_(timeoutMS) {
    // 创建 worker -> 主线程关闭请求通道
    wakeupFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ < 0) {
        throw std::runtime_error("ConnectionManager: eventfd() 失败: " + std::string(strerror(errno)));
    }
}

ConnectionManager::~ConnectionManager() {
    if (wakeupFd_ >= 0) {
        close(wakeupFd_);
        wakeupFd_ = -1;
    }
}

void ConnectionManager::acceptConnection(const int fd, const sockaddr_in& addr) {
    auto& conn = connections_[fd];
    conn.init(fd, addr);
    const uint64_t generation = conn.getGeneration();
    statAccept_.fetch_add(1, std::memory_order_relaxed);
    if (timeoutMS_ > 0) {
        const uint64_t id = timer_.addTimer(fd, timeoutMS_, [this, fd, generation] {
            // 定时器回调：仅在连接仍为同一代际且未在关闭时执行
            const auto it = connections_.find(fd);
            if (it == connections_.end() || it->second.getGeneration() != generation) {
                return;
            }
            if (it->second.isClosing()) {
                return;
            }
            LOGI << "Connection timeout, closing fd " << fd;
            closeConn(&it->second);
        });
        timerId_[fd] = id;
    }
    if (!epoller_.addFd(fd, EPOLLIN | EPOLLONESHOT | EPOLLRDHUP)) {
        LOGE << "addFd failed for fd " << fd << ": " << strerror(errno);
        closeConn(&conn);
    }
}

void ConnectionManager::addClient(const int fd, const sockaddr_in& addr) {
    acceptConnection(fd, addr);
}

void ConnectionManager::dealWrite(HttpConnection* client) {
    extentTime(client);
    statWriteSubmit_.fetch_add(1, std::memory_order_relaxed);
    threadPool_.submit([this, client] {
        if (client->isClosing()) {
            return;
        }
        client->beginTask();

        const auto [ret, err] = client->write();
        bool needClose = false;
        if (client->toWriteBytes() == 0) {
            // 全部写完
            if (client->isKeepAlive() && !client->pipelineDetected()) {
                (void)epoller_.modFd(client->getFd(), EPOLLONESHOT | EPOLLRDHUP | EPOLLIN);
            } else {
                // 非 keep-alive 或检测到 pipeline：写完后关闭
                needClose = true;
            }
        } else if (ret < 0 && err != EAGAIN) {
            // 写错误（EPIPE/ECONNRESET 等）：关闭
            needClose = true;
        } else {
            // EAGAIN（发送缓冲满）或仍有剩余数据（writev 循环阈值内）：继续等可写事件，绝不丢数据
            (void)epoller_.modFd(client->getFd(), EPOLLONESHOT | EPOLLRDHUP | EPOLLOUT);
        }

        client->endTask();
        if (needClose) {
            requestClose(client->getFd(), client->getGeneration());
        }
    });
}

void ConnectionManager::dealRead(HttpConnection* client) {
    extentTime(client);
    statReadSubmit_.fetch_add(1, std::memory_order_relaxed);
    threadPool_.submit([this, client] {
        if (client->isClosing()) {
            return;
        }
        client->beginTask();

        // 读取客户端数据（返回 0 表示对端关闭；EAGAIN 表示暂时无数据）
        const auto [ret, err] = client->read();
        bool needClose = false;
        if (ret <= 0 && err != EAGAIN) {
            // 读异常/EOF：关闭
            needClose = true;
        } else if (client->process()) {
            // 请求解析完成且响应已就绪：等待可写
            (void)epoller_.modFd(client->getFd(), EPOLLONESHOT | EPOLLRDHUP | EPOLLOUT);
        } else {
            // 请求不完整：继续等待读事件
            (void)epoller_.modFd(client->getFd(), EPOLLONESHOT | EPOLLRDHUP | EPOLLIN);
        }

        client->endTask();
        if (needClose) {
            requestClose(client->getFd(), client->getGeneration());
        }
    });
}

void ConnectionManager::requestClose(const int fd, const uint64_t generation) {
    {
        std::lock_guard lock(closeReqMutex_);
        closeReqs_.emplace(fd, generation);
    }
    // eventfd 仅作唤醒信号：写 1 使主线程 epoll_wait 返回
    const uint64_t one = 1;
    const ssize_t n = write(wakeupFd_, &one, sizeof(one));
    if (n != static_cast<ssize_t>(sizeof(one))) {
        LOGE << "requestClose wakeup failed: " << strerror(errno);
    }
}

void ConnectionManager::extentTime(const HttpConnection* client) const {
    if (timeoutMS_ > 0) {
        timer_.resetTimer(client->getFd(), timeoutMS_);
    }
}

void ConnectionManager::closeConn(HttpConnection* client) {
    if (client->isClosing()) {
        return;
    }
    // 级联删除定时器：防止 fd 复用后旧定时器误杀新连接、防止定时器无限堆积
    const int fd = client->getFd();
    if (const auto it = timerId_.find(fd); it != timerId_.end()) {
        timer_.removeTimer(it->second);
        timerId_.erase(it);
    }
    (void)epoller_.delFd(fd);
    // close 内部等待该连接在途任务结束（beginTask/endTask 计数归零）后再释放资源
    client->close();
    statCloseConn_.fetch_add(1, std::memory_order_relaxed);
}

void ConnectionManager::dealWakeup() {
    // 清空唤醒计数（eventfd 计数器语义：多次写入会聚合，这里全部读出）
    uint64_t dummy = 0;
    while (read(wakeupFd_, &dummy, sizeof(dummy)) == static_cast<ssize_t>(sizeof(dummy))) {
        // 仅唤醒，无数据语义
    }
    // 处理队列中的关闭请求（逐个校验代际后关闭）
    std::queue<std::pair<int, uint64_t>> pending;
    {
        std::lock_guard lock(closeReqMutex_);
        pending.swap(closeReqs_);
    }
    while (!pending.empty()) {
        const auto [fd, generation] = pending.front();
        pending.pop();
        const auto it = connections_.find(fd);
        if (it == connections_.end()) {
            continue;
        }
        // fd 代际校验：fd 已被复用为新连接时忽略旧请求
        if (it->second.getGeneration() != generation) {
            continue;
        }
        closeConn(&it->second);
    }
}

HttpConnection* ConnectionManager::find(const int fd) {
    const auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return nullptr;
    }
    return &it->second;
}

void ConnectionManager::closeAll() {
    for (auto& [fd, conn] : connections_) {
        (void)fd;
        if (!conn.isClosing()) {
            closeConn(&conn);
        }
    }
    connections_.clear();
    timerId_.clear();
}

void ConnectionManager::printStats() const {
    LOGI << "STATS accept=" << statAccept_.load() << " readSubmit=" << statReadSubmit_.load()
         << " writeSubmit=" << statWriteSubmit_.load() << " closeConn=" << statCloseConn_.load();
}
