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

#include "WebServer.hpp"

#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/uio.h>

#include "../config/Config.hpp"
#include "../logger/Logger.hpp"
#include "../pool/SqlConnPool.hpp"

WebServer::WebServer(): isClose_(false), timer_(new TimerHeap()), threadPool_(new ThreadPool()),
                        epoller_(new Epoller()) {
    const auto sitePath = Config::get<std::string>("site.path");
    assert(!sitePath.empty());
    const std::filesystem::path path(sitePath);
    assert(std::filesystem::exists(path));
    sitePath_ = absolute(path);
    HttpRequest::preloadAllHtml(sitePath_, true);
    HttpConnection::sitePath = sitePath_;
    HttpConnection::connectionCount = 0;

    const auto logLevel = static_cast<LogLevel>(Config::get<int>("log.level"));
    const auto colorful = Config::get<bool>("log.colorful");
    const auto outputToFile = Config::get<bool>("log.output_to_file");
    Logger::setLogLevel(logLevel);
    if (colorful) {
        Logger::enableColorful();
    } else {
        Logger::disableColorful();
    }
    if (outputToFile) {
        Logger::setOutPutCallback([this](const char *logLine, const int len) {
            asyncLogging_.append(logLine, len);
        });
        asyncLogging_.start();
    }

    LOGI << "Site path: " << sitePath_;

    port_ = Config::get<int>("server.port");
    timeoutMS_ = Config::get<int>("server.timeout");
    // 数据库连接池初始化
    // SqlConnPool::instance()->init(host, port, user, pwd, db, size);
    // 初始化事件和初始化socket(监听)
    // EPOLLRDHUP 检测 socket 连接断开，EPOLLONESHOT 确保每个文件描述符只被一个线程处理
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;
    // 设置为ET边缘触发
    // listenEvent_ |= EPOLLET;
    // connEvent_ |= EPOLLET;
    HttpConnection::isET = (connEvent_ & EPOLLET);
    if (!initSocket()) {
        isClose_ = true;
        LOGE << "Init socket error!";
    }
}

WebServer::~WebServer() {
    close(listenFd_);
    isClose_ = true;
}

void WebServer::start() {
    if (!isClose_) {
        LOGI << "========== Server start ==========";
    }
    int64_t timeout = -1;
    while (!isClose_) {
        if (timeoutMS_ > 0) {
            // 获取下一个连接超时的时间
            timeout = timer_->peek();
        }
        const int eventCounts = epoller_->wait(timeout);
        for (int i = 0; i < eventCounts; ++i) {
            // 处理事件
            const int fd = epoller_->getEventFd(i);
            const uint32_t events = epoller_->getEvents(i);
            if (events & POLLNVAL) {
                LOGE << __FUNCTION__ << ": POLLNVAL";
            }
            if (events & EPOLLERR) {
                assert(connections_.contains(fd));
                LOGE << __FUNCTION__ << ": EPOLLERR";
                closeConn(&connections_[fd]);
            }
            if (events & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
                assert(connections_.contains(fd));
                if (fd == listenFd_) {
                    dealListen();
                } else {
                    dealRead(&connections_[fd]);
                }
            }
            if (events & EPOLLOUT) {
                assert(connections_.contains(fd));
                dealWrite(&connections_[fd]);
            }
        }
    }
}

bool WebServer::initSocket() {
    listenFd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (listenFd_ < 0) {
        LOGE << "Create socket error!";
        return false;
    }

    setSockOptKeepAlive(listenFd_);
    setSockOptReuseAddr(listenFd_);

    // 绑定
    sockaddr_in address{};
    address.sin_family = AF_INET;
    // FIXME: it should be addr.ip() and need some conversion
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port_);
    int ret = bind(listenFd_, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if (ret < 0) {
        LOGE << "Bind Port:" << port_ << " error!";
        close(listenFd_);
        return false;
    }
    // 监听
    ret = listen(listenFd_, SOMAXCONN);
    if (ret < 0) {
        LOGE << "Listen port:" << port_ << " error!";
        close(listenFd_);
        return false;
    }
    // 将监听套接字加入 epoller
    ret = epoller_->addFd(listenFd_, listenEvent_ | EPOLLIN | EPOLLPRI);
    if (ret == 0) {
        LOGE << "Add listen error!";
        close(listenFd_);
        return false;
    }
    LOGI << "Server port:" << port_;
    return true;
}

void WebServer::addClient(const int fd, const sockaddr_in addr) {
    assert(fd > 0);
    connections_[fd].init(fd, addr);
    if (timeoutMS_ > 0) {
        const uint64_t id = timer_->addTimer(fd, timeoutMS_, [this, capture = &connections_[fd]] {
            closeConn(capture);
        });
        (void) id;
    }
    (void) epoller_->addFd(fd, EPOLLIN | connEvent_);
}

void WebServer::dealListen() {
    sockaddr_in clientAddr{};
    socklen_t len = sizeof(sockaddr_in);
    // 使用 accept4 而不是 accept，通过额外的参数 flags 在一次系统调用中完成套接字的接受和属性设置，在并发场景下提升性能并确保原子性。
    const int fd = accept4(listenFd_, reinterpret_cast<sockaddr *>(&clientAddr), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) {
        if (errno == EMFILE) {
            LOGE << __FUNCTION__ << ": To many files opened";
        }
        return;
    }
    assert(fd > 0);
    if (setSockOptKeepAlive(fd) == -1) {
        LOGE << __FUNCTION__ << ": SetSockoptKeepAlive failed";
        close(fd);
        return;
    }
    if (setSockOptNoDelay(fd) == -1) {
        LOGE << __FUNCTION__ << ": SetSockoptNoDelay failed";
        close(fd);
        return;
    }
    if (HttpConnection::connectionCount >= MAX_FD) {
        sendError(fd);
        LOGE << __FUNCTION__ << ": To many clients connected";
        close(fd);
        return;
    }
    sockaddr_in peerAddr{};
    getpeername(fd, reinterpret_cast<sockaddr *>(&peerAddr), &len);
    addClient(fd, peerAddr);
}

void WebServer::dealWrite(HttpConnection *client) const {
    assert(client);
    extentTime(client);
    threadPool_->submit([this, client] {
        onWrite(client);
    });
}

void WebServer::dealRead(HttpConnection *client) const {
    assert(client);
    extentTime(client);
    threadPool_->submit([this, client] {
        onRead(client);
    });
}

void WebServer::sendError(const int fd) {
    assert(fd > 0);
    constexpr char errorMsg[] = "Server error!";
    if (const ssize_t ret = send(fd, errorMsg, strlen(errorMsg), 0); ret < 0) {
        LOGE << "send error to client[" << fd << "] error!";
    }
    close(fd);
}

void WebServer::extentTime(const HttpConnection *client) const {
    assert(client);
    LOGI << __FUNCTION__ << ": Client[" << client->getFd() << "] " << timeoutMS_;
    if (timeoutMS_ > 0) {
        timer_->resetTimer(client->getFd(), timeoutMS_);
    }
}

void WebServer::closeConn(HttpConnection *client) const {
    assert(client);
    (void) epoller_->delFd(client->getFd());
    client->close();
}

void WebServer::onRead(HttpConnection *client) const {
    assert(client);
    // 读取客户端套接字的数据，读到 HttpConnection 的读缓存区
    if (auto [ret, err] = client->read(); ret <= 0 && err != EAGAIN) {
        // 读异常就关闭客户端
        closeConn(client);
        return;
    }
    // 业务逻辑的处理（先读后处理）
    onProcess(client);
}

void WebServer::onWrite(HttpConnection *client) const {
    assert(client);
    auto [ret, err] = client->write();
    LOGD << "client->write() ret:" << ret << " err:" << err;
    if (client->toWriteBytes() == 0) {
        LOGW << "client->toWriteBytes() == 0";
        // 写完了
        if (client->isKeepAlive()) {
            // 切换成监测读事件
            (void) epoller_->modFd(client->getFd(), connEvent_ | EPOLLIN);
            return;
        }
    } else if (ret < 0) {
        if (err == EAGAIN) {
            (void) epoller_->modFd(client->getFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    closeConn(client);
}

void WebServer::onProcess(HttpConnection *client) const {
    // 首先调用 process() 进行逻辑处理，根据返回的信息重新将 fd 置为 EPOLLOUT（写）或 EPOLLIN（读）
    if (client->process()) {
        // 读完事件就通知内核可以写了
        (void) epoller_->modFd(client->getFd(), connEvent_ | EPOLLOUT);
    } else {
        // 写完事件就通知内核可以读了
        (void) epoller_->modFd(client->getFd(), connEvent_ | EPOLLIN);
    }
}
