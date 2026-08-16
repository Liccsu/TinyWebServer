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

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <execinfo.h>
#include <iostream>

#include "WebServer.hpp"

// 供信号处理器访问服务实例；stop() 是线程安全的（原子标志 + eventfd 唤醒）
static std::atomic<WebServer*> g_server{nullptr};

static void gracefulShutdownHandler([[maybe_unused]] const int signum) {
    if (auto* server = g_server.load(std::memory_order_acquire)) {
        server->stop();
    }
}

void segv_signal_handler(const int signum) {
    static constexpr size_t BACKTRACE_SIZE = 256;
    void* buffer[BACKTRACE_SIZE] = {nullptr};

    fprintf(stderr, "\n>>>>>>>>> Catch Signal [%d] <<<<<<<<<\n", signum);
    const int nptrs = backtrace(buffer, BACKTRACE_SIZE);
    char** strings = backtrace_symbols(buffer, nptrs);
    if (!strings) {
        fprintf(stderr, "backtrace_symbols() error");
        exit(-1);
    }

    fprintf(stderr, "======= print backtrace begin =======\n");
    for (int i = 0; i < nptrs; ++i) {
        fprintf(stderr, "[%02d] %s\n", i, strings[i]);
    }
    fprintf(stderr, "======== print backtrace end ========\n\n");

    free(strings);
    exit(-1);
}

int main([[maybe_unused]] const int argc, [[maybe_unused]] const char* argv[]) {
    // 忽略 SIGPIPE：写已关闭的 socket 时由 write 返回 EPIPE，避免进程被信号杀死
    signal(SIGPIPE, SIG_IGN);

#ifdef HANDLE_BACKTRACE
    printf("HANDLE_BACKTRACE ENABLE\n");
    // 需要 GCC 添加 -O0 -rdynamic 编译选项才能得到详细调用栈
    signal(SIGSEGV, segv_signal_handler);
    signal(SIGABRT, segv_signal_handler);
    signal(SIGBUS, segv_signal_handler);
#endif

    // 优雅退出：SIGINT（Ctrl-C）/ SIGTERM（systemd stop 等）触发
    struct sigaction sa{};
    sa.sa_handler = gracefulShutdownHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    try {
        WebServer server{};
        g_server.store(&server, std::memory_order_release);
        server.start();
        g_server.store(nullptr, std::memory_order_release);
    } catch (const std::exception& e) {
        // 配置错误/初始化失败等：给出明确、可定位的错误信息并返回非 0 退出码
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
