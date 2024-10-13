/*
 * Copyright (c) -2024
 * Liccsu
 * All rights reserved.
 *
 * This software is provided under the terms of the GPL License.
 * Please refer to the accompanying LICENSE file for detailed information.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GPL License for more details.
 *
 * For further inquiries, please contact:
 * liccsu@163.com
 */

#include "HttpConnection.hpp"

#include <cassert>
#include <sys/uio.h>

#include "../logger/Logger.hpp"

void HttpConnection::init(const int sockFd, const sockaddr_in &addr) {
    assert(sockFd > 0);
    std::lock_guard lock(mutex_);
    ++connectionCount;
    addr_ = addr;
    fd_ = sockFd;
    writeBuff_.retrieveAll();
    readBuff_.retrieveAll();
    isClose_ = false;
    LOGI << "Client[" << fd_ << "][" << getIp() << ":" << getPort()
        << "] has connected, connection count:" << connectionCount;
}

auto HttpConnection::read() -> std::tuple<ssize_t, int> {
    size_t len;
    int err;
    // 如果是 ET 边沿触发则需要一次性全部读出
    do {
        auto [l, e] = readBuff_.readFd(fd_);
        len = l;
        err = e;
        if (l <= 0) {
            break;
        }
    } while (isET);
    return {len, err};
}

auto HttpConnection::write() -> std::tuple<ssize_t, int> {
    ssize_t len;
    int err = 0;
    do {
        // 将iov的内容写到fd中
        const ssize_t size = writev(fd_, iov_, iovCnt_);
        len = size;
        // 当出现 SIGPIPE 错误时，返回 -1
        if (size < 0) {
            err = errno;
            if (err == EPIPE || err == ECONNRESET) {
                // 连接被关闭或重设
                LOGW << "Connection closed or reset, connection count: " << connectionCount;
            }
            break;
        }
        if (iov_[0].iov_len + iov_[1].iov_len == 0) {
            break;
        }
        if (static_cast<size_t>(size) >= iov_[0].iov_len) {
            // 传输完第一个 iovec
            if (const size_t remaining = size - iov_[0].iov_len; remaining > 0 && iovCnt_ > 1) {
                iov_[1].iov_base = static_cast<uint8_t *>(iov_[1].iov_base) + remaining;
                iov_[1].iov_len -= remaining;
            }
            if (iov_[0].iov_len > 0) {
                writeBuff_.retrieveAll();
                iov_[0].iov_len = 0;
            }
        } else {
            iov_[0].iov_base = static_cast<uint8_t *>(iov_[0].iov_base) + size;
            iov_[0].iov_len -= size;
            writeBuff_.retrieve(size);
        }
    } while (isET || toWriteBytes() > 10240);
    return {len, err};
}

void HttpConnection::close() {
    std::lock_guard lock(mutex_);
    response_.unmapFile();
    if (!isClose_) {
        isClose_ = true;
        --connectionCount;
        ::close(fd_);
        LOGI << "Client[" << fd_ << "][" << getIp() << ":" << getPort()
        << "] quit, client count:" << connectionCount;
    }
}

bool HttpConnection::process() {
    request_.clear();
    if (readBuff_.readableSize() <= 0) {
        return false;
    }
    if (request_.parse(readBuff_)) {
        response_.init(sitePath, request_.path(), request_.isKeepAlive(), 200);
    } else {
        response_.init(sitePath, request_.path(), false, 400);
    }
    // 生成响应报文放入writeBuff_中
    response_.makeResponse(writeBuff_);
    // 响应头
    iov_[0].iov_base = const_cast<char *>(writeBuff_.peek());
    iov_[0].iov_len = writeBuff_.readableSize();
    iovCnt_ = 1;

    // 文件
    if (response_.fileLen() > 0 && response_.file()) {
        iov_[1].iov_base = response_.file();
        iov_[1].iov_len = response_.fileLen();
        iovCnt_ = 2;
    }
    return true;
}
