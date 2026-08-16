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

#include "Epoller.hpp"

#include <cassert>
#include <climits>
#include <sys/epoll.h>
#include <unistd.h>

Epoller::Epoller()
    : Epoller(16) {}

Epoller::Epoller(const int maxEvent)
    : epollFd_(epoll_create1(EPOLL_CLOEXEC)),
      events_(maxEvent) {
    assert(epollFd_ >= 0 && !events_.empty());
}

Epoller::~Epoller() {
    close(epollFd_);
}

bool Epoller::addFd(const int fd, const uint32_t events) const {
    if (fd < 0) {
        return false;
    }
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

bool Epoller::modFd(const int fd, const uint32_t events) const {
    if (fd < 0) {
        return false;
    }
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

bool Epoller::delFd(const int fd) const {
    if (fd < 0) {
        return false;
    }
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
}

int Epoller::wait(const long timeoutMs) {
    // epoll_wait 的 timeout 是 int：超限截断会产生错误语义，先 clamp
    const int timeout = timeoutMs < 0 ? -1 : (timeoutMs > INT_MAX ? INT_MAX : static_cast<int>(timeoutMs));
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeout);
}

int Epoller::getEventFd(const size_t i) const {
    assert(i < events_.size());
    return events_[i].data.fd;
}

uint32_t Epoller::getEvents(const size_t i) const {
    assert(i < events_.size());
    return events_[i].events;
}
