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

#ifndef TINYWEBSERVER_ASYNCLOGGING_HPP
#define TINYWEBSERVER_ASYNCLOGGING_HPP

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../buffer/FixedBuffer.hpp"

class AsyncLogging {
    using LogBuffer = FixedBuffer<LargeBufferSize>;
    using BufferVector = std::vector<std::unique_ptr<LogBuffer> >;
    using BufferPtr = BufferVector::value_type;

    // 每隔 flushInterval_ 秒 flush 一次
    const int flushInterval_;
    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cvBuffersEmpty_;
    // 当前正在写入日志的缓冲区
    BufferPtr currentBuffer_;
    // 预备缓冲区
    BufferPtr nextBuffer_;
    // 已经写满的待同步到磁盘的缓冲区列表
    BufferVector buffers_;

    void threadCallback();

public:
    explicit AsyncLogging(int flushInterval = 3);

    ~AsyncLogging() {
        if (running_) {
            stop();
        }
    }

    void append(const char *logLine, size_t len);

    void start() {
        running_ = true;
        thread_ = std::thread(&AsyncLogging::threadCallback, this);
    }

    void stop() {
        running_ = false;
        cvBuffersEmpty_.notify_all();
        thread_.join();
    }
};


#endif //TINYWEBSERVER_ASYNCLOGGING_HPP
