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

#ifndef TINYWEBSERVER_WEBSERVER_HPP
#define TINYWEBSERVER_WEBSERVER_HPP

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <netinet/tcp.h>

#include "../http/HttpConnection.hpp"
#include "../logger/AsyncLogging.hpp"
#include "../pool/ThreadPool.hpp"
#include "../server/Epoller.hpp"
#include "../timer/TimerHeap.hpp"

class WebServer {
    int port_{};
    int timeoutMS_{};
    bool isClose_;
    int listenFd_{};
    std::string sitePath_;

    static constexpr int MAX_FD = 65536;

    // 监听事件
    uint32_t listenEvent_;
    // 连接事件
    uint32_t connEvent_;

    std::unique_ptr<TimerHeap> timer_;
    std::unique_ptr<ThreadPool> threadPool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, HttpConnection> connections_;
    std::unordered_map<int, uint64_t> timerId_;

    AsyncLogging asyncLogging_;

    bool initSocket();

    void addClient(int fd, sockaddr_in addr);

    void dealListen();

    void dealWrite(HttpConnection *client) const;

    void dealRead(HttpConnection *client) const;

    static void sendError(int fd);

    void extentTime(const HttpConnection *client) const;

    void closeConn(HttpConnection *client) const;

    void onRead(HttpConnection *client) const;

    void onWrite(HttpConnection *client) const;

    void onProcess(HttpConnection *client) const;

    static int setSockOptKeepAlive(const int fd) {
        constexpr int optVal = 1;
        return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optVal, sizeof(optVal));
    }

    static int setSockOptReuseAddr(const int fd) {
        constexpr int optVal = 1;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
    }

    static int setSockOptNoDelay(const int fd) {
        constexpr int optVal = 1;
        return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optVal, sizeof(optVal));
    }

public:
    WebServer();

    ~WebServer();

    void start();
};


#endif //TINYWEBSERVER_WEBSERVER_HPP
