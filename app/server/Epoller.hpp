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

#ifndef TINYWEBSERVER_EPOLLER_HPP
#define TINYWEBSERVER_EPOLLER_HPP

#include <cstdint>
#include <vector>
#include <sys/epoll.h>

class Epoller {
    int epollFd_;
    std::vector<epoll_event> events_{};

public:
    Epoller();

    explicit Epoller(int maxEvent);

    ~Epoller();

    [[nodiscard]]
    bool addFd(int fd, uint32_t events) const;

    [[nodiscard]]
    bool modFd(int fd, uint32_t events) const;

    [[nodiscard]]
    bool delFd(int fd) const;

    int wait(long timeoutMs = -1);

    [[nodiscard]]
    int getEventFd(size_t i) const;

    [[nodiscard]]
    uint32_t getEvents(size_t i) const;
};


#endif //TINYWEBSERVER_EPOLLER_HPP
