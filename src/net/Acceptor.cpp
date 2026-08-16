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

#include "Acceptor.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

Acceptor::Acceptor(std::string host, const int port)
    : host_(std::move(host)),
      port_(port) {
    if (port_ <= 0 || port_ > 65535) {
        throw std::runtime_error("acceptor: 非法端口 " + std::to_string(port_));
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (listenFd_ < 0) {
        throw std::runtime_error("acceptor: socket() 失败: " + std::string(strerror(errno)));
    }

    setSockOptKeepAlive(listenFd_);
    setSockOptReuseAddr(listenFd_);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    if (host_.empty() || host_ == "0.0.0.0" || host_ == "*") {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1) {
        const std::string msg = "acceptor: 非法监听地址: " + host_;
        close(listenFd_);
        listenFd_ = -1;
        throw std::runtime_error(msg);
    }
    address.sin_port = htons(static_cast<uint16_t>(port_));
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        const std::string msg = "acceptor: bind 端口 " + std::to_string(port_) + " 失败: " + strerror(errno);
        close(listenFd_);
        listenFd_ = -1;
        throw std::runtime_error(msg);
    }
    if (listen(listenFd_, SOMAXCONN) < 0) {
        const std::string msg = "acceptor: listen 失败: " + std::string(strerror(errno));
        close(listenFd_);
        listenFd_ = -1;
        throw std::runtime_error(msg);
    }
}

Acceptor::~Acceptor() {
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

int Acceptor::accept() {
    // accept4：一次系统调用完成接受与属性设置（SOCK_NONBLOCK|SOCK_CLOEXEC）
    return accept4(listenFd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
}

int Acceptor::setSockOptKeepAlive(const int fd) {
    constexpr int optVal = 1;
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optVal, sizeof(optVal));
}

int Acceptor::setSockOptReuseAddr(const int fd) {
    constexpr int optVal = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
}

int Acceptor::setSockOptNoDelay(const int fd) {
    constexpr int optVal = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optVal, sizeof(optVal));
}
