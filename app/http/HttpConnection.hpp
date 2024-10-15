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

#ifndef TINYWEBSERVER_HTTPCONNECTION_H
#define TINYWEBSERVER_HTTPCONNECTION_H

#include <mutex>
#include <tuple>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "../buffer/Buffer.hpp"

class HttpConnection {
    int fd_ = -1;
    sockaddr_in addr_{};

    inline static std::mutex mutex_;
    bool isClose_ = true;

    int iovCnt_{};
    iovec iov_[2]{};

    // 读缓冲区
    Buffer readBuff_;
    // 写缓冲区
    Buffer writeBuff_;

    HttpRequest request_;
    HttpResponse response_;

public:
    inline static bool isET;
    inline static std::string sitePath;
    inline static int connectionCount;

    HttpConnection() = default;

    ~HttpConnection() {
        close();
    }

    void init(int sockFd, const sockaddr_in &addr);

    auto read() -> std::tuple<ssize_t, int>;

    auto write() -> std::tuple<ssize_t, int>;

    void close();

    [[nodiscard]]
    int getFd() const {
        return fd_;
    }

    [[nodiscard]]
    int getPort() const {
        return addr_.sin_port;
    }

    [[nodiscard]]
    const char *getIp() const {
        return inet_ntoa(addr_.sin_addr);
    }

    bool process();

    // 写的总长度
    [[nodiscard]]
    size_t toWriteBytes() const {
        return iov_[0].iov_len + iov_[1].iov_len;
    }

    [[nodiscard]]
    bool isKeepAlive() const {
        return request_.isKeepAlive();
    }
};


#endif //TINYWEBSERVER_HTTPCONNECTION_H
