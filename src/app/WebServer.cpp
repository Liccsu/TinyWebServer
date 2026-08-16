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

#include "WebServer.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

#include "../concurrency/ThreadPool.hpp"
#include "../config/Config.hpp"
#include "ConnectionManager.hpp"

WebServer::WebServer()
    : epoller_(new Epoller()),
      timer_(new TimerHeap()),
      threadPool_(new ThreadPool(configuredThreadCount())) {
    // 请求大小限制（可选配置；缺省使用安全默认值）
    HttpRequest::maxBodySize = Config::getWithDefault<size_t>("server.max_body_size", static_cast<size_t>(1024) * 1024);
    HttpRequest::maxRequestSize =
        Config::getWithDefault<size_t>("server.max_request_size", static_cast<size_t>(8) * 1024 * 1024);

    const auto sitePath = Config::get<std::string>("site.path");
    if (sitePath.empty()) {
        throw std::runtime_error("config: site.path 不能为空");
    }
    const std::filesystem::path path(sitePath);
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("config: site.path 不存在: " + sitePath);
    }
    sitePath_ = std::filesystem::absolute(path);
    // 规范化（消除 "./"、".." 等段），确保 HttpResponse 的 realpath 前缀校验一致
    std::error_code ec;
    const auto canon = std::filesystem::weakly_canonical(sitePath_, ec);
    if (!ec) {
        sitePath_ = canon.string();
    }
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
        Logger::setOutPutCallback(
            [this](const char* logLine, const int len) { asyncLogging_.append(logLine, static_cast<size_t>(len)); });
        asyncLogging_.start();
        asyncLoggingStarted_ = true;
    }

    LOGI << "Site path: " << sitePath_;

    const int timeoutMS = [&] {
        const int t = Config::get<int>("server.timeout");
        if (t < 0) {
            throw std::runtime_error("config: server.timeout 不能为负数，当前值: " + std::to_string(t));
        }
        return t;
    }();

    // 数据库连接池按需惰性初始化（仅登录/注册时），此处不强制启动

    // 边缘触发默认关闭（LT 模式）；如需 ET，取消注释并确保 read/write 循环读取
    HttpConnection::isET = false;

    const int port = Config::get<int>("server.port");
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("config: server.port 必须在 1-65535 之间，当前值: " + std::to_string(port));
    }
    const auto host = Config::getWithDefault<std::string>("server.host", "0.0.0.0");
    maxConnections_ = Config::getWithDefault<int>("server.max_connections", 65536);
    if (maxConnections_ <= 0) {
        throw std::runtime_error("config: server.max_connections 必须为正数，当前值: " +
                                 std::to_string(maxConnections_));
    }
    acceptor_ = std::make_unique<Acceptor>(host, port);
    connections_ = std::make_unique<ConnectionManager>(*epoller_, *timer_, *threadPool_, timeoutMS);

    if (!epoller_->addFd(acceptor_->fd(), EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        throw std::runtime_error("acceptor: 监听 fd 加入 epoll 失败: " + std::string(strerror(errno)));
    }
    if (!epoller_->addFd(connections_->wakeupFd(), EPOLLIN)) {
        throw std::runtime_error("acceptor: wakeup fd 加入 epoll 失败: " + std::string(strerror(errno)));
    }
    LOGI << "Server listening on " << host << ":" << port << " (max connections " << maxConnections_ << ")";
}

WebServer::~WebServer() {
    stop();
    // 解除日志回调对 this 的捕获（cleanup 在事件循环退出时已解绑，此处覆盖析构路径）
    Logger::resetOutPutCallback();
}

void WebServer::start() {
    LOGI << "========== Server start ==========";
    int64_t timeout = -1;
    while (!isClose_) {
        if (timer_->peek() >= 0) {
            // 获取下一个连接超时的时间（peek 会先执行所有到期回调）
            timeout = timer_->peek();
        }
        const int eventCounts = epoller_->wait(timeout);
        const auto loopStart = std::chrono::steady_clock::now();
        for (int i = 0; i < eventCounts; ++i) {
            const int fd = epoller_->getEventFd(i);
            const uint32_t events = epoller_->getEvents(i);

            // wakeup 通道：处理 worker 上报的关闭请求
            if (fd == connections_->wakeupFd()) {
                connections_->dealWakeup();
                continue;
            }

            // 监听套接字：只处理新连接
            if (fd == acceptor_->fd()) {
                if (events & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
                    dealListen();
                }
                continue;
            }

            HttpConnection* client = connections_->find(fd);
            if (client == nullptr) {
                // 未知 fd（连接已关闭但事件残留）：忽略
                LOGW << "Unknown fd event: " << fd;
                continue;
            }

            // 错误/挂起：立即关闭，不再处理同一 fd 的其余事件
            if (events & (EPOLLERR | EPOLLHUP)) {
                LOGE << "EPOLLERR/EPOLLHUP on fd " << fd;
                connections_->closeConn(client);
                continue;
            }

            // 对端关闭写端：有可读数据则读完（read 返回 0 后关闭），否则直接关闭
            if (events & EPOLLRDHUP) {
                if (events & EPOLLIN) {
                    connections_->dealRead(client);
                } else {
                    connections_->closeConn(client);
                }
                continue;
            }

            if (events & EPOLLIN) {
                connections_->dealRead(client);
            }
            if (events & EPOLLOUT) {
                connections_->dealWrite(client);
            }
        }
        const auto loopCostMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - loopStart).count();
        if (loopCostMs > SLOW_LOOP_MS) {
            LOGE << "SLOW EVENT LOOP: " << loopCostMs << "ms for " << eventCounts << " events";
        }
    }
    cleanup();
}

int WebServer::configuredThreadCount() {
    const int configured = Config::getWithDefault<int>("server.threads", 0);
    if (configured > 0) {
        return configured;
    }
    const unsigned int autoCount = std::thread::hardware_concurrency() * 2;
    return autoCount > 0 ? static_cast<int>(autoCount) : 8;
}

void WebServer::dealListen() {
    // accept 直到 EAGAIN（LT 模式下一次事件可能对应多个待接受连接）
    while (true) {
        const int fd = acceptor_->accept();
        if (fd < 0) {
            if (errno == EMFILE) {
                LOGE << "Too many open files, accept failed";
            }
            // EAGAIN：无更多待接受连接
            break;
        }
        if (Acceptor::setSockOptKeepAlive(fd) == -1) {
            LOGE << "SetSockoptKeepAlive failed on fd " << fd;
            close(fd);
            continue;
        }
        if (Acceptor::setSockOptNoDelay(fd) == -1) {
            LOGE << "SetSockoptNoDelay failed on fd " << fd;
            close(fd);
            continue;
        }
        if (HttpConnection::connectionCount.load() >= maxConnections_) {
            LOGW << "Too many clients connected (limit " << maxConnections_ << "), rejecting fd " << fd;
            close(fd);
            continue;
        }
        sockaddr_in peerAddr{};
        socklen_t len = sizeof(sockaddr_in);
        getpeername(fd, reinterpret_cast<sockaddr*>(&peerAddr), &len);
        connections_->acceptConnection(fd, peerAddr);
    }
}

void WebServer::cleanup() {
    LOGI << "Server stopping, closing connections";
    connections_->closeAll();
    timer_->clear();
    threadPool_->shutdown();
    if (asyncLoggingStarted_) {
        asyncLogging_.stop();
        // 解除对 WebServer 的捕获，防止后续日志访问已析构对象
        Logger::resetOutPutCallback();
    }
    connections_->printStats();
    LOGI << "Server stopped";
}

void WebServer::stop() {
    if (!isClose_.exchange(true)) {
        // 通过 eventfd 唤醒阻塞中的 epoll_wait，事件循环随即退出
        const uint64_t one = 1;
        const ssize_t n = write(connections_->wakeupFd(), &one, sizeof(one));
        (void)n;
    }
}
