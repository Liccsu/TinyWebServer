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

#ifndef TINYWEBSERVER_HTTPCONNECTION_H
#define TINYWEBSERVER_HTTPCONNECTION_H

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <netinet/in.h>
#include <tuple>

#include "../net/Buffer.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

class HttpConnection {
    int fd_ = -1;
    sockaddr_in addr_{};
    // 连接代际：fd 复用防护。worker 上报的关闭请求携带 (fd, generation)，
    // 主线程校验代际匹配后才关闭，避免误杀复用同一 fd 的新连接。
    uint64_t generation_ = 0;

    // 连接是否已关闭（fd 已关闭）
    std::atomic<bool> isClose_{true};
    // 是否有线程池任务正在处理本连接（onRead/onWrite）
    std::atomic<int> inFlight_{0};
    // 关闭请求已发出：不再接受新的处理任务
    std::atomic<bool> closing_{false};

    // pipeline 检测：响应写完必须关闭连接（本服务器不支持 pipeline）
    bool pipelineDetected_ = false;
    // 请求体超限：直接返回 413
    bool requestTooLarge_ = false;

    // 任务计数等待条件变量：closeConn 等待 inFlight_ 归零后释放资源
    std::mutex taskMutex_;
    std::condition_variable taskCv_;

    int iovCnt_{};
    std::array<iovec, 2> iov_{};

    // 读缓冲区
    Buffer readBuff_;
    // 写缓冲区
    Buffer writeBuff_;

    HttpRequest request_;
    HttpResponse response_;

public:
    inline static std::atomic<int> connectionCount{0};
    inline static bool isET = false;
    inline static std::string sitePath;

    HttpConnection() = default;

    ~HttpConnection() { close(); }

    void init(int sockFd, const sockaddr_in& addr);

    // 从 fd 读入读缓冲；返回 {读到的字节数, errno}。
    // 返回值 0 表示对端关闭（EOF）；<0 且 err==EAGAIN 表示暂时无数据。
    auto read() -> std::tuple<ssize_t, int>;

    // 将 iov 数据写出；返回 {本次写入字节数, errno}
    auto write() -> std::tuple<ssize_t, int>;

    // 处理读缓冲中的请求并生成响应。
    // 返回 true = 响应已就绪（应等待 EPOLLOUT）；false = 请求不完整，继续等待读事件。
    bool process();

    // 标记连接开始/结束一个线程池任务；closeConn 会等待任务全部结束
    void beginTask();
    void endTask();

    [[nodiscard]]
    bool isClosing() const {
        return closing_.load(std::memory_order_acquire);
    }

    // 仅主线程调用：关闭连接。会等待在途任务结束后再释放 fd 与 mmap 资源。
    void close();

    [[nodiscard]]
    int getFd() const {
        return fd_;
    }

    [[nodiscard]]
    uint64_t getGeneration() const {
        return generation_;
    }

    [[nodiscard]]
    int getPort() const {
        return addr_.sin_port;
    }

    [[nodiscard]]
    const char* getIp() const {
        return inet_ntoa(addr_.sin_addr);
    }

    // 写的总长度
    [[nodiscard]]
    size_t toWriteBytes() const {
        return iov_[0].iov_len + iov_[1].iov_len;
    }

    [[nodiscard]]
    bool isKeepAlive() const {
        return request_.isKeepAlive();
    }

    // pipeline 检测：存在未消费的后续请求数据，响应后必须关闭连接
    [[nodiscard]]
    bool pipelineDetected() const {
        return pipelineDetected_;
    }
};

#endif // TINYWEBSERVER_HTTPCONNECTION_H
