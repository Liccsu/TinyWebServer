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

#ifndef TINYWEBSERVER_ACCEPTOR_HPP
#define TINYWEBSERVER_ACCEPTOR_HPP

#include <string>

// 监听与接受新连接（仅主线程使用）。
// RAII：构造时创建非阻塞监听 socket 并 bind/listen，析构时关闭。
// 构造失败抛出 std::runtime_error（含 errno 说明）。
class Acceptor {
    int listenFd_ = -1;
    std::string host_;
    int port_ = 0;

public:
    // host: IPv4 地址或空串（INADDR_ANY 通配）
    Acceptor(std::string host, int port);

    ~Acceptor();

    Acceptor(const Acceptor&) = delete;

    Acceptor& operator=(const Acceptor&) = delete;

    [[nodiscard]]
    int fd() const {
        return listenFd_;
    }

    // accept4 一个新连接（SOCK_NONBLOCK|SOCK_CLOEXEC）；
    // 返回 fd；失败返回 -1（errno 保留，如 EMFILE）
    [[nodiscard]]
    int accept();

    // 连接级 socket 选项
    static int setSockOptKeepAlive(int fd);

    static int setSockOptNoDelay(int fd);

    static int setSockOptReuseAddr(int fd);
};

#endif // TINYWEBSERVER_ACCEPTOR_HPP
