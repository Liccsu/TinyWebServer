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

#include "HttpConnection.hpp"

#include "../log/Logger.hpp"

#include <cassert>
#include <chrono>
#include <sys/uio.h>

void HttpConnection::init(const int sockFd, const sockaddr_in& addr) {
    assert(sockFd > 0);
    addr_ = addr;
    fd_ = sockFd;
    writeBuff_.retrieveAll();
    readBuff_.retrieveAll();
    request_.clear();
    isClose_.store(false, std::memory_order_release);
    closing_.store(false, std::memory_order_release);
    pipelineDetected_ = false;
    requestTooLarge_ = false;
    ++generation_;
    connectionCount.fetch_add(1, std::memory_order_relaxed);
    LOGI << "Client[" << fd_ << "][" << getIp() << ":" << getPort()
         << "] connected, count:" << connectionCount.load(std::memory_order_relaxed);
}

auto HttpConnection::read() -> std::tuple<ssize_t, int> {
    ssize_t len = 0;
    int err = 0;
    // 如果是 ET 边沿触发则需要一次性全部读出
    do {
        auto [l, e] = readBuff_.readFd(fd_);
        len = l;
        err = e;
        if (l <= 0) {
            break;
        }
        // 读缓冲上限防御：恶意客户端持续发送时停止读入并置 413
        if (readBuff_.readableSize() > HttpRequest::maxRequestSize) {
            LOGW << "Request too large on fd " << fd_;
            requestTooLarge_ = true;
            break;
        }
    } while (isET);
    return {len, err};
}

auto HttpConnection::write() -> std::tuple<ssize_t, int> {
    ssize_t len = 0;
    int err = 0;
    do {
        // 将 iov 的内容写到 fd
        const ssize_t size = writev(fd_, iov_.data(), iovCnt_);
        len = size;
        if (size < 0) {
            err = errno;
            if (err == EPIPE || err == ECONNRESET) {
                // 连接被关闭或重设
                LOGW << "Connection closed or reset, fd: " << fd_;
            }
            break;
        }
        if (iov_[0].iov_len + iov_[1].iov_len == 0) {
            break;
        }
        if (static_cast<size_t>(size) >= iov_[0].iov_len) {
            // 传输完第一个 iovec（响应头）
            if (const size_t remaining = static_cast<size_t>(size) - iov_[0].iov_len; remaining > 0 && iovCnt_ > 1) {
                iov_[1].iov_base = static_cast<uint8_t*>(iov_[1].iov_base) + remaining;
                iov_[1].iov_len -= remaining;
            }
            if (iov_[0].iov_len > 0) {
                writeBuff_.retrieveAll();
                iov_[0].iov_len = 0;
            }
        } else {
            // 响应头部分写出：推进指针并同步消费写缓冲
            iov_[0].iov_base = static_cast<uint8_t*>(iov_[0].iov_base) + size;
            iov_[0].iov_len -= static_cast<size_t>(size);
            writeBuff_.retrieve(static_cast<size_t>(size));
        }
    } while (isET || toWriteBytes() > 10240);
    return {len, err};
}

bool HttpConnection::process() {
    if (isClosing()) {
        return false;
    }

    // 请求体超限：直接生成 413 响应
    if (requestTooLarge_) {
        response_.init(sitePath, "/", false, 413, false);
        response_.makeResponse(writeBuff_);
        iov_[0].iov_base = const_cast<char*>(writeBuff_.peek());
        iov_[0].iov_len = writeBuff_.readableSize();
        iovCnt_ = 1;
        return true;
    }

    request_.clear();
    if (readBuff_.readableSize() <= 0) {
        // 无数据可读：等待读事件
        return false;
    }

    const ParseResult result = request_.parse(readBuff_);
    if (result == ParseResult::NeedMore) {
        // 请求不完整：等待更多数据（绝不能误报 400）
        return false;
    }

    const bool isHead = (request_.getMethodEnum() == HttpRequest::Method::Head);
    const bool isHealthz = (result == ParseResult::Ok && request_.getPath() == "/healthz");
    if (isHealthz) {
        // 健康检查端点：返回 200 纯文本，不访问文件系统
        response_.makeTextResponse(writeBuff_, "ok\n", request_.isKeepAlive(), isHead);
    } else if (result == ParseResult::Ok) {
        response_.init(sitePath, request_.getPath(), request_.isKeepAlive(), 200, isHead);
    } else {
        // 解析失败：返回错误状态码，连接写完即关闭
        LOGW << "Parse error, status: " << request_.statusCode() << ", fd: " << fd_;
        response_.init(sitePath, "/", false, request_.statusCode(), isHead);
    }

    // 生成响应报文放入 writeBuff_（healthz 已在 makeTextResponse 中生成）
    if (!isHealthz) {
        response_.makeResponse(writeBuff_);
    }
    iov_[0].iov_base = const_cast<char*>(writeBuff_.peek());
    iov_[0].iov_len = writeBuff_.readableSize();
    iovCnt_ = 1;

    // 静态文件（HEAD 请求不发送 body）
    if (!isHead && response_.fileLen() > 0 && response_.file()) {
        iov_[1].iov_base = response_.file();
        iov_[1].iov_len = response_.fileLen();
        iovCnt_ = 2;
    }

    // 完整请求解析后缓冲仍有剩余数据：说明客户端在 pipeline 请求。
    // 本服务器不完整支持 pipeline：响应结束后关闭连接，丢弃剩余数据（安全、可预测）。
    if (result == ParseResult::Ok && readBuff_.readableSize() > 0) {
        LOGW << "Pipelined data after request on fd " << fd_ << ", will close after response";
        pipelineDetected_ = true;
    }
    return true;
}

void HttpConnection::beginTask() {
    inFlight_.fetch_add(1, std::memory_order_acq_rel);
}

void HttpConnection::endTask() {
    if (inFlight_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard lock(taskMutex_);
        taskCv_.notify_all();
    }
}

void HttpConnection::close() {
    // 等待在途任务结束，确保任务不再访问本连接资源后再释放
    {
        std::unique_lock lock(taskMutex_);
        const auto waitStart = std::chrono::steady_clock::now();
        taskCv_.wait(lock, [this] { return inFlight_.load(std::memory_order_acquire) == 0; });
        const auto waitMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - waitStart).count();
        if (waitMs > 100) {
            LOGE << "close() waited " << waitMs << "ms for in-flight task on fd " << fd_;
        }
    }
    closing_.store(true, std::memory_order_release);
    response_.unmapFile();
    if (!isClose_.exchange(true)) {
        connectionCount.fetch_sub(1, std::memory_order_relaxed);
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        LOGI << "Client closed, count:" << connectionCount.load(std::memory_order_relaxed);
    }
}
