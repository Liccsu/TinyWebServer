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

#include <csignal>
#include <cstdio>
#include <execinfo.h>
#include <iostream>

#include "app/server/WebServer.hpp"

void segv_signal_handler(const int signum) {
    static constexpr size_t BACKTRACE_SIZE = 256;
    void *buffer[BACKTRACE_SIZE] = {nullptr};

    fprintf(stderr, "\n>>>>>>>>> Catch Signal [%d] <<<<<<<<<\n", signum);
    const int nptrs = backtrace(buffer, BACKTRACE_SIZE);
    char **strings = backtrace_symbols(buffer, nptrs);
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

int main([[maybe_unused]] const int argc, [[maybe_unused]] const char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
#ifdef HANDLE_BACKTRACE
    printf("HANDLE_BACKTRACE ENABLE\n");
    // 需要 GCC 添加 -O0 -rdynamic 编译选项才能得到详细调用栈
    signal(SIGSEGV, segv_signal_handler);
    signal(SIGABRT, segv_signal_handler);
    signal(SIGBUS, segv_signal_handler);
#endif

    WebServer server{};
    server.start();

    return 0;
}
